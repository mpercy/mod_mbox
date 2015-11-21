/* Copyright 2015 The Apache Software Foundation or its licensors, as
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

/* mod_mbox javascript functions. */

// Global variables.
var g_is_dragging = false;
var g_active_id = '';

function updateMessageContent(msgHtml, linkEl) {
  var msgViewInner = $("#msgview-inner");
  if (msgViewInner.length > 0) {
    // Typical case.
    msgViewInner.replaceWith(msgHtml);
  } else {
    // First time user clicks a message.
    $("#msglist-outer").append(msgHtml);
  }
  // Append a "close" link at the top of the message.
  $("#msgview tr.from td.right").prepend('<a href="#" id="close-link">hide</a>');
  $("#close-link").click(function(event) {
    hideMessagePanel(event);
  });
  // Append a "permalink" as well.
  $("#msgview tr.from td.right").append(' <span id="msg-permalink">(<a href="#">permalink</a>)</span>');
  $("#msg-permalink a").attr("href", linkEl.href);
}

// Handle moving the drag bar.
function handleMoveDragBar(event) {
  var msgListOuter = $("#msglist-outer");
  var msgListOuterTop = msgListOuter.offset().top;
  var msgListOuterHeight = msgListOuter.height();
  var dragBarHeight = $("#dragbar").height();
  var msgListInnerTop = parseInt($("#msglist-inner").offset().top);
  var msgListRelOffset = msgListInnerTop - msgListOuterTop;
  var usableHeight = msgListOuterHeight - msgListRelOffset;

  var relDragLocation = parseInt(event.pageY - msgListInnerTop);
  if (relDragLocation < 0) relDragLocation = 0;
  if (relDragLocation > usableHeight - (dragBarHeight - 2)) relDragLocation = usableHeight - (dragBarHeight - 2);
  var newMsgListHeight = relDragLocation - parseInt(dragBarHeight / 2);
  var newMsgViewHeight = msgListOuterHeight - msgListRelOffset - newMsgListHeight - dragBarHeight - 1;
  $("#msglist-inner").height(newMsgListHeight);
  $("#msgview-inner").height(newMsgViewHeight);
}

function createDragBar() {
  if ($("#dragbar").length) {
    return;
  }
  $("#msglist-outer").append('<div id="dragbar"><div class="mid"></div></div>');
  $("#dragbar").mousedown(function(event) {
    event.preventDefault();
    g_is_dragging = true;
    $(document).mousemove(handleMoveDragBar);
  });
}

function resizeBoxList() {
  var windowHeight = parseInt($(window).height());
  var boxListOuter = $("#boxlist-outer");
  var boxListOuterTop = boxListOuter.offset().top;
  var boxListOuterHeight = windowHeight - boxListOuterTop;
  boxListOuter.height(boxListOuterHeight);

  var boxList = $("#boxlist-inner");
  var boxListTop = boxList.offset().top;
  var boxListRelOffset = boxListTop - boxListOuterTop;
  boxList.height(boxListOuterHeight - boxListRelOffset);
}

// Called when the browser window is resized.
function handleResizeWindow(event) {
  if (!$("html").hasClass("ajax")) {
    return;
  }

  // Resize the left column.
  resizeBoxList();

  // Resize the right column.
  var newWindowHeight = $(window).height();
  var msgListOuter = $("#msglist-outer");
  var msgListInner = $("#msglist-inner");
  var dragBar = $("#dragbar");
  var msgView = $("#msgview-inner");

  var msgListOuterTop = msgListOuter.offset().top;
  var newContainerHeight = newWindowHeight - msgListOuterTop;
  msgListOuter.height(newContainerHeight);

  var msgListTop = msgListInner.offset().top;
  var msgListHeight = msgListInner.height();
  var dragBarHeight = dragBar.height();
  var msgViewHeight = msgView.height();
  var msgListRelOffset = msgListTop - msgListOuterTop;

  if (newContainerHeight <= msgListRelOffset + dragBarHeight) {
    msgListInner.height(0);
    msgView.height(0);
  } else if (newContainerHeight > msgListHeight + dragBarHeight + msgListRelOffset) {
    msgView.height(newContainerHeight - msgListHeight - dragBarHeight - msgListRelOffset);
  } else {
    msgListInner.height(newContainerHeight - dragBarHeight - msgListRelOffset);
    msgView.height(0);
  }
}

function hideMessagePanel(event) {
  event.preventDefault();
  event.stopPropagation();
  cancelActiveLink();
  var origScrollTop = $("#msglist-inner").scrollTop();
  $("#msglist-ajax-header").remove();
  $("html").removeClass("ajax");
  $("#boxlist-inner").css("height", "");
  $("#boxlist-outer").css("height", "");
  $("#msglist-outer").css("height", "");
  $("#msglist-inner").css("height", "");
  $("#msgview-inner").css("display", "none");
  $(document).scrollTop(origScrollTop);
}

// Determine the correct placement of the message list header.
function resizeMsgListHeader() {
  // We really only have to correct the width of the Date column.
  var colWidth = $("#msglist td.date").first().width();
  var scrollbarWidth = $("#msglist-outer").width() - $("#msglist").width() - 5; // borders plus... i dunno
  $("#msglist-ajax-header th.date").width(colWidth + scrollbarWidth);
}

function renderMsgListHeader() {
  $("#msglist-ajax-header").remove();
  $("#msglist-inner").before('<table id="msglist-ajax-header"></table>');
  $("#msglist thead").clone().appendTo("#msglist-ajax-header");
}

function renderMessageContainer(linkEl, origScrollTop, origLinkOffsetTop) {
  var previouslyEnabled = $("html").hasClass("ajax");
  $("html").addClass("ajax");

  // Resize the left column.
  resizeBoxList();

  // Render the sorting buttons.
  renderMsgListHeader();

  // Resize the right column's height.
  var windowHeight = parseInt($(window).height());
  var msgListOuter = $("#msglist-outer");
  var msgListOuterTop = parseInt(msgListOuter.offset().top);
  var msgListOuterHeight = windowHeight - msgListOuterTop;
  msgListOuter.height(msgListOuterHeight);

  var msgListInner = $("#msglist-inner");
  var dragBar = $("#dragbar");
  var msgView = $("#msgview-inner");

  var msgListInnerTop = msgListInner.offset().top;
  var msgListRelOffset = msgListInnerTop - msgListOuterTop;
  var usableHeight = msgListOuterHeight - msgListRelOffset - dragBar.height();
  // Only explicitly set the split location when they first open the UI.
  if (!previouslyEnabled) {
    msgListInner.height(parseInt(usableHeight * .35));
  }
  msgView.height(usableHeight - msgListInner.height());

  resizeMsgListHeader();

  // Only scroll when we first open the menu.
  if (previouslyEnabled) return;

  // Scroll so that the link is in the middle of the msglist pane.
  var msgListInnerHeight = msgListInner.height()
  var newScrollTop = origLinkOffsetTop - msgListInnerTop
                   - (msgListInnerHeight / 2) + 5;
  $.Deferred(function( defer ) {
    // First, snap to the position the page was in when the link was clicked,
    // to avoid disorienting the user.
    $("#msglist-inner").animate({ scrollTop: origScrollTop }, 0, defer.resolve );
  }).done(function() {
    // Then, animate a scroll so that the link ends up in the middle of the
    // msglist pane. However, only do this if the link is not fully within the
    // bounds of the msglist pane already.
    var linkScrollTop = origLinkOffsetTop - msgListInnerTop;
    var msgListInnerScrollTop = $("#msglist-inner").scrollTop();
    // 13px font and extra 10px of wiggle room.
    if (linkScrollTop - 10 < msgListInnerScrollTop ||
        linkScrollTop + 23 > msgListInnerScrollTop + msgListInnerHeight) {
      $("#msglist-inner").animate({ scrollTop: newScrollTop }, 300);
    }
  });
}

function displayMessage(msg, linkEl, origScrollTop, origLinkOffsetTop) {
  createDragBar();
  updateMessageContent(msg, linkEl);
  renderMessageContainer(linkEl, origScrollTop, origLinkOffsetTop);
}

function handleAjaxError(jqXHR, textStatus, errorThrown) {
  alert("Error fetching message.");
}

function isLinkActive() {
  return g_active_id !== '';
}

function getActiveLinkTableRow() {
  var linkEl = document.getElementById(g_active_id);
  return linkEl.parentNode.parentNode;
}

function cancelActiveLink() {
  if (!isLinkActive()) return;
  $(getActiveLinkTableRow()).removeClass("active");
  g_active_id = '';
}

function selectFirstMessage() {
  fetchMessage($("#msglist td.subject a").get(0).id, 0, 0);
}

var PREV_MESSAGE = -1;
var NEXT_MESSAGE = 1;
function iterElementSibling(el, direction) {
  switch (direction) {
    case NEXT_MESSAGE:
      return el.nextElementSibling;
    case PREV_MESSAGE:
      return el.previousElementSibling;
    default:
      return null;
  }
}

function selectAdjacentMessage(event, direction) {
  event.preventDefault();
  event.stopPropagation();
  if (!isLinkActive()) {
    selectFirstMessage();
    return;
  }
  var rowEl = getActiveLinkTableRow();
  while (rowEl = iterElementSibling(rowEl, direction)) {
    var link = $(rowEl).find("td.subject a");
    if (link.length) {
      fetchMessage(link.get(0).id, 0, 0);
      break;
    }
  }
}

function nextMessage(event) {
  selectAdjacentMessage(event, NEXT_MESSAGE);
}

function prevMessage(event) {
  selectAdjacentMessage(event, PREV_MESSAGE);
}

function setActiveLink(linkEl) {
  cancelActiveLink();
  g_active_id = linkEl.id;
  var tableRow = $(linkEl).parent().parent();
  tableRow.addClass("active");
  linkEl.focus();
}

// Fetch the mail message corresponding to the href in the link element with id
// 'link_id'.
function fetchMessage(link_id, origScrollTop, origLinkOffsetTop) {
  var linkEl = document.getElementById(link_id);
  setActiveLink(linkEl);

  // The 'skip_chrome' query parameter causes the server to return only the
  // msgbox table HTML, not the rest of the page.
  var ajaxUrl = linkEl.href + "?skip_chrome=1";
  $.ajax({url: ajaxUrl, timeout: 10000, success: function(data) {
    displayMessage(data, linkEl, origScrollTop, origLinkOffsetTop);
  }}).fail(handleAjaxError);
}

// Attach event handlers to all thread links in the msglist table.
// When the thread links are clicked, we'll show a popup window with the
// content instead of going to the URL.
function attachEventHandlers() {
  $(window).resize(function(event) {
    handleResizeWindow(event);
  });

  $(document).mouseup(function(event) {
    if (!g_is_dragging) return;
    $(document).unbind("mousemove");
    g_is_dragging = false;
  });

  $(document).keydown(function(event) {
    // Ignore event any time modifiers are being used.
    if (event.ctrlKey || event.shiftKey || event.altKey || event.metaKey) {
      return;
    }
    switch (event.keyCode) {
      case 27: // Escape key
        hideMessagePanel(event);
        break;
      case 74: // 'j'
        nextMessage(event);
        break;
      case 75: // 'k'
        prevMessage(event);
        break;
    }
  });

  $("#msglist td.subject a").each(function() {
    var id = this.id;
    var tr = this.parentNode.parentNode;
    $(tr, this).bind("click", function(event) {
      event.preventDefault();
      event.stopPropagation();
      fetchMessage(id, $(window).scrollTop(), $(this).offset().top);
    });
  });
}

$(document).ready(attachEventHandlers());
