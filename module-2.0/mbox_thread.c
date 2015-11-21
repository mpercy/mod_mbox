/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* Based on Jamie Zawinski's description of the Netscape 3.x threading
 * algorithm at <http://www.jwz.org/doc/threading.html>.
 */

#include "mbox_thread.h"
#include "mbox_sort.h"
#include "apr_lib.h"

/*
 * Determines if a string is a reply
 */
static int is_reply_string(char *c)
{
    /* Match the following cases: Re:, RE:, RE[1]:, Re: Re[2]: Re: */
    if (c) {
        if ((c[0] == 'R') && (c[1] == 'E' || c[1] == 'e')) {
            if (c[2] == ':')
                return 1;
            else if (c[2] == '[') {
                c = c + 3;

                while (apr_isdigit(*c))
                    c++;

                if (*c == ']')
                    return 1;
            }
        }
    }
    return 0;
}

/*
 * Determines if a message is a reply
 */
static int is_reply(Message *m)
{
    return ((m && m->subject) ? is_reply_string(m->subject) : 0);
}

/*
 * Strips all of the RE: junk from the subject line.
 */
static char *strip_subject(apr_pool_t *p, Message *m)
{
    char *newVal, *match = m->subject, *tmp = NULL;

    /* Match the following cases: Re:, RE:, RE[1]:, Re: Re[2]: Re: */
    while (match && *match) {
        /* When we don't have a match, tmp contains the "real" subject. */
        tmp = newVal = match;
        match = NULL;
        if (*newVal == 'R' && (*++newVal == 'e' || *newVal == 'E')) {
            /* Note to self.  In pure compressed syntax, the famous dangling
             * else occurs.  Oh, well. */
            if (*++newVal == '[') {
                while (apr_isdigit(*++newVal)) {
                }
                if (*++newVal == ']' && *++newVal == ':')
                    match = ++newVal;
            }
            else if (*newVal == ':')
                match = ++newVal;
        }

        if (match)
            while (apr_isspace(*match))
                match++;
    }

    return apr_pstrdup(p, tmp);
}

/*
 * Detects if the needle can be reached from either the haystack's
 * next or children.
 */
static int detect_loop(Container *haystack, Container *needle)
{
    if (!haystack || !needle)
        return 0;

    if (haystack == needle)
        return 1;

    if (haystack->next && detect_loop(haystack->next, needle))
        return 1;

    if (haystack->child && detect_loop(haystack->child, needle))
        return 1;

    return 0;
}

static void unlink_parent(Container *c)
{
    Container *next;

    if (c->parent->child == c)
        c->parent->child = c->next;
    else {
        next = c->parent->child;

        /* If we go past the end of the list,
         * we are in trouble. */
        while (next->next != c)
            next = next->next;

        next->next = c->next;
    }
}

static void prune_container(Container *c)
{
    Container *nextChild, *lastChild, *tmpChild;

    lastChild = NULL;
    nextChild = c->child;

    while (nextChild) {
        if (!nextChild->message) {
            while (nextChild->child) {  /* Promote children to this level */
                /* Save what would be our next child to visit */
                tmpChild = nextChild->child->next;

                nextChild->child->parent = nextChild->parent;

                nextChild->child->next = nextChild->next;
                nextChild->next = nextChild->child;

                nextChild->child = tmpChild;
            }

            /* Remove this blank container from the chain */
            if (lastChild)
                lastChild->next = nextChild->next;
            else
                c->child = nextChild->next;
        }
        else {
            prune_container(nextChild);

            lastChild = nextChild;
        }

        nextChild = nextChild->next;
    }
}

/*
 * Takes the right container and makes all children children of the left.
 */
static void join_container(Container *l, Container *r)
{
    Container *next, *tmp;
    if (!r)
        return;

    next = r->child;
    while (next) {
        tmp = next;
        next = next->next;
        tmp->next = l->child;
        tmp->parent = l;
        l->child = tmp;
    }
}

/*
 * Takes the right container and makes it a child of the left.
 */
static void append_container(Container *l, Container *r)
{
    if (!r)
        return;

    r->parent = l;
    r->next = l->child;
    l->child = r;
}

/*
 * Takes two containers and returns them as one with sibling relationship.
 */
static Container *merge_container(apr_pool_t *p, Container *l, Container *r)
{
    Container *c, *next;

    c = (Container *) apr_pcalloc(p, sizeof(Container));
    r->parent = c;
    l->parent = c;

    /* Update parents of siblings */
    if (r->next) {
        next = r->next;
        while (next) {
            next->parent = c;
            next = next->next;
        }
    }

    if (!l->next)
        l->next = r;
    else {
        next = l->next;
        while (next->next) {
            next->parent = c;
            next = next->next;
        }
        next->parent = c;
        next->next = r;
    }

    c->child = l;

    return c;
}

static void delete_from_hash(apr_pool_t *p, apr_hash_t *h, void *i)
{
    apr_hash_index_t *hashIndex;
    void *hashKey, *hashVal;
    apr_ssize_t hashLen;

    for (hashIndex = apr_hash_first(p, h); hashIndex;
         hashIndex = apr_hash_next(hashIndex)) {
        apr_hash_this(hashIndex, (void *) &hashKey, &hashLen, &hashVal);
        if (hashVal == i) {
            apr_hash_set(h, hashKey, hashLen, NULL);
            return;
        }
    }
}

/*
 * Comparison function called by mbox_sort_linked_list
 */
static int compare_siblings(void *p, void *q, void *pointer)
{
    Container *a = (Container *) p;
    Container *b = (Container *) q;

    /* The definition of the containers give us the following rules. */
    if (!a->message)
        a = a->child;
    if (!b->message)
        b = b->child;

    return ((a->message->date > b->message->date) ? 1 : -1);
}

/*
 * Sorts the siblings by date and returns the first item in the list.
 */
static Container *sort_siblings(Container *c)
{
    Container *sibling = c;

    /* We need to sort all of our siblings' children */
    while (sibling) {
        if (sibling->child)
            sibling->child = sort_siblings(sibling->child);
        sibling = sibling->next;
    }

    /* Sort us and our siblings */
    return (Container *) mbox_sort_linked_list(c, 3, compare_siblings,
                                               NULL, NULL);
}

/*
 * Calculates the threading relationships for a list of messages
 */
Container *calculate_threads(apr_pool_t *p, MBOX_LIST *l)
{
    apr_hash_t *h, *rootSet, *subjectSet;
    apr_hash_index_t *hashIndex;
    MBOX_LIST *current = l;
    const apr_array_header_t *refHdr;
    apr_table_entry_t *refEnt;
    Message *m;
    Container *c, *subjectPair, *realParent, *curParent, *tmp;
    void *hashKey, *hashVal, *subjectVal;
    char *subject;
    int msgIDLen, refLen, i;
    apr_ssize_t hashLen, subjectLen;

    /* FIXME: Use APR_HASH_KEY_STRING instead?  Maybe slower. */
    h = apr_hash_make(p);

    while (current != NULL) {
        m = (Message *) current->value;
        msgIDLen = strlen(m->msgID);
        c = (Container *) apr_hash_get(h, m->msgID, msgIDLen);
        if (c) {
            c->message = m;
        }
        else {
            c = (Container *) apr_pcalloc(p, sizeof(Container));
            c->message = m;
            c->parent = NULL;
            c->child = NULL;
            c->next = NULL;
            apr_hash_set(h, m->msgID, msgIDLen, c);
        }

        realParent = NULL;

        if (m->references) {
            refHdr = apr_table_elts(m->references);
            refEnt = (apr_table_entry_t *) refHdr->elts;

            for (i = 0; i < refHdr->nelts; i++) {

                refLen = strlen(refEnt[i].key);

                curParent =
                    (Container *) apr_hash_get(h, refEnt[i].key, refLen);

                /* Create a dummy node to store the message we haven't
                 * yet seen. */
                if (!curParent) {
                    curParent =
                        (Container *) apr_pcalloc(p, sizeof(Container));
                    apr_hash_set(h, refEnt[i].key, refLen, curParent);
                }

                /* Check to make sure we are not going to create a loop
                 * by adding this parent to our list.
                 */
                if (realParent &&
                    !detect_loop(curParent, realParent) &&
                    !detect_loop(realParent, curParent)) {
                    /* Update the parent */
                    if (curParent->parent)
                        unlink_parent(curParent);

                    curParent->parent = realParent;
                    curParent->next = realParent->child;
                    realParent->child = curParent;
                }

                /* We now have a new parent */
                realParent = curParent;
            }

        }

        /* The last parent we saw is our parent UNLESS it causes a loop. */
        if (realParent && !detect_loop(c, realParent) &&
            !detect_loop(realParent, c)) {
            /* We need to unlink our parent's link to us. */
            if (c->parent)
                unlink_parent(c);

            c->parent = realParent;
            c->next = realParent->child;
            realParent->child = c;
        }

        current = current->next;
    }

    /* Find the root set */
    rootSet = apr_hash_make(p);

    for (hashIndex = apr_hash_first(p, h); hashIndex;
         hashIndex = apr_hash_next(hashIndex)) {
        apr_hash_this(hashIndex, (void *) &hashKey, &hashLen, &hashVal);
        c = (Container *) hashVal;
        if (!c->parent)
            apr_hash_set(rootSet, hashKey, hashLen, c);
    }

    /* Prune empty containers */
    for (hashIndex = apr_hash_first(p, rootSet); hashIndex;
         hashIndex = apr_hash_next(hashIndex)) {
        apr_hash_this(hashIndex, (void *) &hashKey, &hashLen, &hashVal);
        c = (Container *) hashVal;

        prune_container(c);

        if (!c->message && !c->child)
            apr_hash_set(rootSet, hashKey, hashLen, NULL);
    }

    /* Merge root set by subjects */
    subjectSet = apr_hash_make(p);

    for (hashIndex = apr_hash_first(p, rootSet); hashIndex;
         hashIndex = apr_hash_next(hashIndex)) {
        apr_hash_this(hashIndex, (void *) &hashKey, &hashLen, &hashVal);
        c = (Container *) hashVal;

        /* If we don't have a message, our child will. */
        if (!c->message)
            c = c->child;

        subject = strip_subject(p, c->message);
        subjectLen = strlen(subject);

        /* FIXME: Match what JWZ says */
        subjectVal = apr_hash_get(subjectSet, subject, subjectLen);
        if (subjectVal) {
            if (!c->message)
                apr_hash_set(subjectSet, subject, strlen(subject), hashVal);
            else {
                subjectPair = (Container *) subjectVal;
                if (!is_reply(c->message) && is_reply(subjectPair->message))
                    apr_hash_set(subjectSet, subject, strlen(subject),
                                 hashVal);
            }
        }
        else
            apr_hash_set(subjectSet, subject, strlen(subject), hashVal);
    }

    /* Subject table now populated */
    for (hashIndex = apr_hash_first(p, rootSet); hashIndex;
         hashIndex = apr_hash_next(hashIndex)) {
        apr_hash_this(hashIndex, (void *) &hashKey, &hashLen, &hashVal);
        c = (Container *) hashVal;

        /* If we don't have a message, our child will. */
        if (c->message)
            subject = strip_subject(p, c->message);
        else
            subject = strip_subject(p, c->child->message);

        subjectLen = strlen(subject);

        subjectVal = apr_hash_get(subjectSet, subject, subjectLen);
        subjectPair = (Container *) subjectVal;

        /* If we need to merge the tables */
        if (subjectPair && subjectPair != c) {
            if (!c->message || !subjectPair->message) { /* One is dummy */
                if (!c->message && !subjectPair->message)
                    join_container(subjectPair, c);
                else if (c->message && !subjectPair->message) {
                    /* It's possible that we're already a child! */
                    if (c->parent != subjectPair)
                        append_container(subjectPair, c);
                }
                else {          /* (!c->message && subjectPair->message) */

                    append_container(c, subjectPair);
                    apr_hash_set(subjectSet, subject, subjectLen, c);
                    delete_from_hash(p, rootSet, subjectPair);
                }
            }
            else {              /* Both aren't dummies */

                /* We are Reply */
                if (is_reply(c->message) && !is_reply(subjectPair->message))
                    append_container(subjectPair, c);
                else if (!is_reply(c->message) &&
                         is_reply(subjectPair->message)) {
                    append_container(c, subjectPair);
                    apr_hash_set(subjectSet, subject, subjectLen, c);
                    delete_from_hash(p, rootSet, subjectPair);
                }
                else {          /* We are both replies. */

                    c = merge_container(p, c, subjectPair);
                    apr_hash_set(subjectSet, subject, subjectLen, c);
                    delete_from_hash(p, rootSet, subjectPair);
                }
            }
        }
    }

    /* Now, we are done threading.  We want to return a sorted container
     * back to our caller.  All children of the root set need to be in
     * order and then we need to issue an ordering to the root set.
     */
    tmp = NULL;
    /* Sort siblings */
    for (hashIndex = apr_hash_first(p, subjectSet); hashIndex;
         hashIndex = apr_hash_next(hashIndex)) {
        apr_hash_this(hashIndex, (void *) &hashKey, &hashLen, &hashVal);
        c = (Container *) hashVal;

        sort_siblings(c);

        if (tmp)
            c->next = tmp;
        tmp = c;
    }

    return (Container *) mbox_sort_linked_list(tmp, 3, compare_siblings, NULL,
                                               NULL);
}
