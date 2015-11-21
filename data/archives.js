/* Copyright 2001-2005 The Apache Software Foundation or its licensors, as
* applicable.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

/* mod_mbox javascript functions.
 */

var body = null;
var _mbox = '';       /* Current browsed mailbox */
var _baseURI = '';    /* Base archives URI */
var _sort = 'thread'; /* Sort mode defaults to 'thread' */
var _page = 0;

var _boxlist = null;
var _msglist = null;

var _default_ctx = 3;


/* I *hate* doing this kind of things */
function checkBrowser ()
{
  /* Gecko only ... we're still working on it !
   *
   * Safari will report itself as 'Gecko', but it will crash with AJAX.
   * So, ignore it too.
   */
  if (navigator.product == 'Gecko' &&
      navigator.vendor != "Apple Computer, Inc.") {
    return true;
  }

  return false;
}

/* Returns a XmlHttpObject, or false on error. Don't forget to use
 * different variable names for XMLHttpRequest object instances since
 * we are doing asynchronous Javascript and JS does not support
 * variable scope (local variables). */
function getHTTPObject ()
{
  var xmlhttp = false;

  /*@cc_on
    @if (@_jscript_version >= 5)

  try {
    xmlhttp = new ActiveXObject("Msxml2.XMLHTTP");
  } catch (e) {
    try {
      xmlhttp = new ActiveXObject("Microsoft.XMLHTTP");
    } catch (E) {
      xmlhttp = false;
    }
  }

    @else
  xmlhttp = false;
    @end
    @*/

  if (!xmlhttp && typeof XMLHttpRequest != 'undefined') {
    try {
      xmlhttp = new XMLHttpRequest();
    } catch (e) {
      xmlhttp = false;
    }
  }

  return xmlhttp;
}

/* onLoad function for boxes list : changes 'Threads - Date - Author'
 * links into a single 'Browser' link. */
function indexLinks ()
{
  var i, entries;
  var http = getHTTPObject ();

  /* Do not change links if the browser does not support
     XMLHttpRequest */
  if (!checkBrowser () || !http) {
    return false;
  }

  i = 0;

  /* Fetch all spans in the document */
  entries = document.getElementsByTagName('span');

  /* Update span.links contents */
  while (entries[i]) {
    if (entries[i].className == 'links') {
      entries[i].innerHTML = '<a href="' + entries[i].getAttribute('id') +
	'.mbox/browser" title="Dynamic browser">Browse</a>';
    }
    i++;
  }

  return true;
}

/* Returns the textual representation of a month number. */
function getMonthName (num, full)
{
  var full_months = new Array("January", "February", "March",
			      "April", "May", "June", "July",
			      "August", "September", "October",
			      "November", "December");

  var short_months = new Array("Jan", "Feb", "Mar", "Apr",
			       "May", "Jun", "Jul", "Aug",
			       "Sep", "Oct", "Nov", "Dec");

  if (full) {
    return full_months[num-1];
  }

  return short_months[num-1];
}

/* Returns the message list table header, including page selector and
 * sort links */
function getMsgListHeader ()
{
  /* Fetch current page and page count */
  var current_page = parseInt(_msglist.getAttribute('page'));
  var pages = parseInt(_msglist.getAttribute('pages'));

  var str = '<thead><tr><th class="title"><a href="' + _sort + '?' + _page +
    '" title="Switch to flat view">Message list</a></th>';

  /* Page selector */
  str += '<th class="pages">';

  if (current_page) {
    str += '<a href="browser" onclick="javascript:getMsgList(\'' + _sort + '\', \'' +
      (current_page - 1) + '\'); return false;">' +
      '&laquo; Previous</a> &middot; ';
  }

  if (pages > 1) {
    for (i = 0; i<pages ; i++) {
      if (i) {
	str += ' &middot; ';
      }

      if (i != current_page) {
	str += '<a href="browser" onclick="javascript:getMsgList(\'' + _sort + '\', \'' +
	  i + '\'); return false;">' + (i+1) + '</a>';
      }
      else {
	str += (i+1);
      }
    }
  }

  if (current_page+1 < pages) {
    str += ' &middot; <a href="browser" onclick="javascript:getMsgList(\'' + _sort + '\', \'' +
      (current_page + 1) + '\'); return false;">' +
      'Next &raquo;</a>';
  }

  str += '</th>';

  str += '<th class="sort">';
  if (_sort == 'thread') {
    str += 'Thread &middot; ' +
      '<a href="browser" onclick="javascript:getMsgList(\'author\', null); return false;">Author</a>' +
      ' &middot; ' +
      '<a href="browser" onclick="javascript:getMsgList(\'date\', null); return false;">Date</a>';
  }
  else if (_sort == 'author') {
    str += '<a href="browser" onclick="javascript:getMsgList(\'thread\', null); return false;">Thread</a>' +
      ' &middot; Author &middot; ' +
      '<a href="browser" onclick="javascript:getMsgList(\'date\', null); return false;">Date</a>';
  }
  else {
    str += '<a href="browser" onclick="javascript:getMsgList(\'thread\', null); return false;">Thread</a>' +
      ' &middot; ' +
      '<a href="browser" onclick="javascript:getMsgList(\'author\', null); return false;">Author</a>' +
      ' &middot; Date';
  }

  str += '</th></tr></thead>';

  return str;
}

/* Returns a table-formatted message list entry */
function buildMessageListEntry (msg, msg_num)
{
  var linked = parseInt(msg.getAttribute('linked'));
  var depth  = parseInt(msg.getAttribute('depth'));
  var i;

  var str = '<tr id="msg-' + msg_num + '">';

  /* The author */
  str += '<td class="author">';
  if (linked) {
    str += msg.getElementsByTagName('from')[0].firstChild.data;
  }
  str += '</td>';

  /* The subject */
  str += '<td class="subject">';
  for (i=0 ; i<depth ; i++) {
    str += '&nbsp;&nbsp;';
  }

  if (linked) {
    str += '<a href="ajax/' + encodeURIComponent(msg.getAttribute('id')) +
      '" onclick="javascript:toggleMessage(' + msg_num + '); return false;">';
  }

  str += msg.getElementsByTagName('subject')[0].firstChild.data;

  if (linked) {
    str += '</a>';
  }
  str += '</td>';

  /* The message date */
  str += '<td class="date">';
  if (linked) {
    str += msg.getElementsByTagName('date')[0].firstChild.data;
  }
  str += '</td>';

  str += '</tr>';

  return str;
}

function drawFullMsgList ()
{
  /* Destroy previous message view if displayed */
  if (document.getElementById('msgview')) {
    body.removeChild(document.getElementById('msgview'));
  }

  /* Get an array of all messages */
  var msgs = _msglist.getElementsByTagName('message');

  /* Create the destination table */
  var msglist = document.createElement('table');
  msglist.setAttribute('id', 'msglist');

  var str = getMsgListHeader ();

  /* Build message list entries */
  str += '<tbody>';

  i = 0;
  while (msgs[i]) {
    str += buildMessageListEntry (msgs[i], i);
    i++;
  }

  str += '</tbody>';
  msglist.innerHTML = str;

  if (document.getElementById('msglist')) {
    body.removeChild(document.getElementById('msglist'));
  }

  body.appendChild(msglist);
}

/* Retreive the message list from server, according to the sort flag
 * and page number given */
function getMsgList (sort, page)
{
  var msglist_http = getHTTPObject ();

  if (!msglist_http) {
    return false;
  }

  if (!sort) {
    sort = _sort;
  }
  if (!page) {
    page = _page;
  }

  _page = page;
  _sort = sort;

  /* The handler */
  msglist_http.onreadystatechange = function () {
    if (msglist_http.readyState == 4) { /* 4 : "complete" */
      if (msglist_http.status == 200) { /* 200 : OK */
	_msglist = msglist_http.responseXML.documentElement;
	var i;

	drawFullMsgList ();

	/* Remove now useless loading message */
	body.removeChild(document.getElementById('loading'));
      }
    }
  }

  msglist_http.open('GET', 'ajax/' + sort + '?' + page, true);
  msglist_http.send(null);

  return true;
}

/* Parses the message's MIME structure. This is a recursive function,
   maybe we should flatten it. */
function parseMimeStructure (id, mime)
{
  var count = 0;
  var i = 0;
  var str = '';

  while (mime[i]) {
    /* If the node is a MIME part, output its entry */
    if (mime[i].nodeName == "part") {
      var length = parseInt(mime[i].getAttribute('length'));

      str += '<li>';

      if (length) {
	str += '<a href="' + _baseURI + _mbox + '/raw/' + id +
	  mime[i].getAttribute('link') + '" rel="nofollow">';
      }

      var name = mime[i].getAttribute('name');
      if (name) {
	str += name;
      }
      else {
	str += 'Unnamed ' + mime[i].getAttribute('ct');
      }

      if (length) {
	str += '</a>';
      }

      str += ' (' + mime[i].getAttribute('cd') + ', ' +
	mime[i].getAttribute('cte') + ', ' + length + ' bytes)</li>';

      count++;
    }

    /* Otherwise it's a MIME multipart, recurse */
    else if (mime[i].nodeName == "mime") {
      str += '<ul>' +
	parseMimeStructure (id, mime[i].childNodes) + '</ul>';
    }

    i++;
  }

  return str;
}

function getMessageView(id, msg_num)
{
	var str = '<tr><th class="title"><a href="' + id +
	'" title="Direct link to this message">Permalink (Message view)</a></th><th class="nav">';

	if (msg_num) {
	  str += '<a href="browser" onclick="javascript:toggleMessage(' + (msg_num-1) +
	    '); return false;" title="View previous message">Previous</a> &middot; ';
	}

	if (msg_num+1 < _msglist.getElementsByTagName('message').length) {
	  str += '<a href="browser" onclick="javascript:toggleMessage(' + (msg_num+1) +
	  '); return false;" title="View next message">Next</a> &middot; ';
	}

	str += '<a href="browser" onclick="javascript:closeMessage(); return false;" ' +
	'title="Close message">x</a></th></tr>';

  return str;
}

/* Get a message */
function getMessage (id, msg_num)
{
  var message_http = getHTTPObject ();

  if (!message_http) {
    return false;
  }

  /* The handler */
  message_http.onreadystatechange = function () {
    if (message_http.readyState == 4) { /* 4 : "complete" */
      if (message_http.status == 200) { /* 200 : OK */
	var message = message_http.responseXML.documentElement;

	/* Create the destination table */
	var msgview = document.createElement('table');
	msgview.setAttribute('id', 'msgview');
	msgview.setAttribute('class', 'js');

	var str = '<thead>' + getMessageView(id, msg_num) + '</thead>';

	str += '<tbody>';

	str += '<tr class="from"><td class="left">From</td><td class="right">' +
	message.getElementsByTagName('from')[0].firstChild.data + '</td></tr>';
	str += '<tr class="subject"><td class="left">Subject</td><td class="right">' +
	message.getElementsByTagName('subject')[0].firstChild.data + '</td></tr>';
	str += '<tr class="date"><td class="left">Date</td><td class="right">' +
	message.getElementsByTagName('date')[0].firstChild.data + '</td></tr>';

	str += '<tr class="contents"><td colspan="2"><pre>' +
	message.getElementsByTagName('contents')[0].firstChild.data + '</pre></td></tr>';

	/* MIME structure */
	var mime = message.getElementsByTagName('mime')[0].childNodes;

	str += '<tr class="mime"><td class="left">Mime</td><td class="right"><ul>' +
	parseMimeStructure (id, mime) +
	'</ul></td></tr>';

	str += '<tr class="raw"><td class="left"></td><td class="right">' +
	'<a href="' + _baseURI + _mbox + '/raw/' + id + '" rel="nofollow">View raw message</a></td></tr>';

	str += '</tbody>';

	str += '<tfoot>' + getMessageView(id, msg_num) + '</tfoot>';

	msgview.innerHTML = str;

	/* Place our msgview just under the msglist */
	msgview.style.top = document.getElementById('msglist').offsetHeight + 'px';
	msgview.style.display = 'table';

	/* And display the msgview */
	body.appendChild(msgview);
	document.documentElement.scrollTop = 0;

	/* Remove now useless loading message */
	body.removeChild(document.getElementById('loading'));
      }
    }
  }

  message_http.open('GET', 'ajax/' + id, true);
  message_http.send(null);

  return true;
}

/* Toggle the view of a message. Change the message list into a
 * context view, and call message view table creation. */
function toggleMessage (msg_num)
{
  var msgs = _msglist.getElementsByTagName('message');
  var id = encodeURIComponent(msgs[msg_num].getAttribute('id'));
  var i;

  var min = msg_num - _default_ctx;
  var max = msg_num + _default_ctx;

  /* Check context id values */
  if (min < 0) {
    min = 0;
  }

  /* Destroy previous message list table */
  if (document.getElementById('msglist')) {
    body.removeChild(document.getElementById('msglist'));
  }

  var msglist = document.createElement('table');
  msglist.setAttribute('id', 'msglist');

  var str = getMsgListHeader ();
  str += '<tbody>';

  /* Build list */
  for (i=min ; i<=max ; i++) {
    if (msgs[i]) {
      str += buildMessageListEntry (msgs[i], i);
    }
  }

  str += '</tbody></table>';
  msglist.innerHTML = str;
  body.appendChild(msglist);
  document.getElementById('msg-' + msg_num).setAttribute('class', 'msgactive');

  /* Display a loading box ... */
  body.innerHTML += '<div id="loading">Loading ...</div>';

  /* Now, display the message */
  if (document.getElementById('msgview')) {
    body.removeChild(document.getElementById('msgview'));
  }

  if (!getMessage (id, msg_num)) {
    body.innerHTML += '<table class="js" id="msgview">' +
      '<thead><tr><th>Message view</th></tr></thead>' +
      '<tbody><tr><td>Unable to load message ' + id + ' !</td></tr></tbody>' +
      '</table>';
  }
}

function closeMessage ()
{
  /* First, destroy the message view window */
  if (document.getElementById('msgview')) {
    body.removeChild(document.getElementById('msgview'));
  }

  /* Next, destroy previous message list table */
  if (document.getElementById('msglist')) {
    body.removeChild(document.getElementById('msglist'));
  }

  /* Finally, redraw full message list */
  drawFullMsgList ();
}

function buildBoxListEntry (box)
{
  var id = box.getAttribute('id');

  /* The decimal base (10) is passed to parseInt in order to avoid
     octal parsing due to leading 0. */
  var month = parseInt(id.substr(4, 2), 10);
  var str = '';

  /* If the mbox id is the same as the one who called the
     browser, set the entry as active */
  if (id == _mbox) {
    str += '<tr id="boxactive">';
  }
  else {
    str += '<tr>';
  }

  /* Build link (_baseURI/id/browser) */
  str += '<td class="box">';

  if (id == _mbox) {
    str += '<a href="browser" onclick="javascript:drawFullMsgList(); return false;"';
  }
  else {
    str += '<a href="' + _baseURI + id + '/browser"';
  }

  str += ' title="Browse ' + getMonthName(month, true) + ' ' + id.substr(0, 4) +
    ' archives">' + getMonthName(month, false) + ' ' + id.substr(0, 4) + '</a></td>';

  /* Finally end the entry with the message count */
  str += '<td class="msgcount">' + box.getAttribute('count') +
    '</td></tr>';

  return str;
}

/* Retreive and parse the box list.
 */
function getBoxList ()
{
  var boxlist_http = getHTTPObject ();

  if (!boxlist_http) {
    return false;
  }

  /* The handler */
  boxlist_http.onreadystatechange = function () {
    if (boxlist_http.readyState == 4) { /* 4 : "complete" */
      if (boxlist_http.status == 200) { /* 200 : OK */
	_boxlist = boxlist_http.responseXML.documentElement;

	/* Get an array of all mbox entries */
	var boxes = _boxlist.getElementsByTagName('mbox');

	/* Create the destination table */
	var boxlist = document.createElement('table');
	boxlist.setAttribute('id', 'boxlist');

	var str = '<thead><tr><th colspan="2">Box list</th></tr></thead>';
	str += '<tbody>';

	/* Parse boxes array */
	var i = 0;
	while (boxes[i]) {
	  str += buildBoxListEntry (boxes[i]);
	  i++;
	}

	str += '</tbody>';
	boxlist.innerHTML = str;

	body.appendChild(boxlist);
      }
    }
  }

  boxlist_http.open('GET', 'ajax/boxlist', true);
  boxlist_http.send(null);

  return true;
}

/* onLoad function for browser. Call the message list and box list
 * creation functions. */
function loadBrowser (baseURI)
{
  _baseURI = baseURI.substring(0, baseURI.lastIndexOf('/')+1);
  _mbox = baseURI.substring(baseURI.lastIndexOf('/')+1);

  if (!checkBrowser ()) {
    window.location = _baseURI + _mbox + '/' + _sort;
    return true;
  }

  body = document.getElementsByTagName('body')[0];
  body.innerHTML += '<div id="loading">Loading ...</div>';

  document.getElementsByTagName('h1')[0].innerHTML += ': ' +
    getMonthName(parseInt(_mbox.substr(4, 2), 10), true) + ' ' +
    _mbox.substr(0, 4);

  /* Get message list */
  if (!getMsgList (null, null)) {
    body.innerHTML += '<table id="msglist">' +
      '<thead><tr><th>Message list</th></tr></thead>' +
      '<tbody><tr><td>Unable to load message list for ' +
      _mbox + ' !</td></tr></tbody></table>';
  }

  /* Get box list */
  if (!getBoxList ()) {
    body.innerHTML += '<table id="boxlist">' +
      '<thead><tr><th>Box list</th></tr></thead>' +
      '<tbody><tr><td>Unable to load box list !</td></tr></tbody>' +
      '</table>';
  }

  return true;
}
