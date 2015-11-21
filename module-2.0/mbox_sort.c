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

/*
 * This SLL sort is based on the public-domain algorithm by Philip
 * J. Erdelsky (pje@acm.org).  You may find the algorithm and notes at:
 * http://www.efgh.com/software/llmsort.htm
 */

#include <stdio.h> /* defines NULL */
#include "mbox_sort.h"

/*
 * Merge-sort implementation tweaked for single-linked-list (SLL).
 *
 * pointer and pcount are optional parameters (MAY be NULL).
 *
 * @param p Head of the linked list to sort.
 * @param index Location of the pointer to the next element within the struct.
 * @param compare Pointer to comparison function.
 * @param pointer Pointer to optional parameter passed to comparison function.
 * @param pcount Pointer to a long that may contain the count
 */
void *mbox_sort_linked_list(void *p, unsigned index,
                            int (*compare) (void *, void *, void *),
                            void *pointer, unsigned long *pcount)
{
    unsigned base;
    unsigned long block_size;

    struct record
    {
        struct record *next[1];
        /* other members not directly accessed by this function */
    };

    struct tape
    {
        struct record *first, *last;
        unsigned long count;
    } tape[4];

    /* Distribute the records alternately to tape[0] and tape[1]. */

    tape[0].count = tape[1].count = 0L;
    tape[0].first = tape[1].first = NULL;
    base = 0;
    while (p != NULL) {
        struct record *next = ((struct record *) p)->next[index];
        ((struct record *) p)->next[index] = tape[base].first;
        tape[base].first = ((struct record *) p);
        tape[base].count++;
        p = next;
        base ^= 1;
    }

    /* If the list is empty or contains only a single record, then */
    /* tape[1].count == 0L and this part is vacuous.               */

    for (base = 0, block_size = 1L; tape[base + 1].count != 0L;
         base ^= 2, block_size <<= 1) {
        int dest;
        struct tape *tape0, *tape1;
        tape0 = tape + base;
        tape1 = tape + base + 1;
        dest = base ^ 2;
        tape[dest].count = tape[dest + 1].count = 0;
        for (; tape0->count != 0; dest ^= 1) {
            unsigned long n0, n1;
            struct tape *output_tape = tape + dest;
            n0 = n1 = block_size;
            while (1) {
                struct record *chosen_record;
                struct tape *chosen_tape;
                if (n0 == 0 || tape0->count == 0) {
                    if (n1 == 0 || tape1->count == 0)
                        break;
                    chosen_tape = tape1;
                    n1--;
                }
                else if (n1 == 0 || tape1->count == 0) {
                    chosen_tape = tape0;
                    n0--;
                }
                else if ((*compare) (tape0->first, tape1->first, pointer) > 0) {
                    chosen_tape = tape1;
                    n1--;
                }
                else {
                    chosen_tape = tape0;
                    n0--;
                }
                chosen_tape->count--;
                chosen_record = chosen_tape->first;
                chosen_tape->first = chosen_record->next[index];
                if (output_tape->count == 0)
                    output_tape->first = chosen_record;
                else
                    output_tape->last->next[index] = chosen_record;
                output_tape->last = chosen_record;
                output_tape->count++;
            }
        }
    }

    if (tape[base].count > 1L)
        tape[base].last->next[index] = NULL;
    if (pcount != NULL)
        *pcount = tape[base].count;
    return tape[base].first;
}
