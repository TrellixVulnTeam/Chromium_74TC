/*
 * Copyright (C) 2007, 2008 Apple Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// For testing.
WebInspector.panels = [];

/**
 * @extends {WebInspector.VBox}
 * @constructor
 */
WebInspector.Panel = function(name)
{
    WebInspector.VBox.call(this);

    this.element.classList.add("panel");
    this.element.setAttribute("aria-label", name);
    this.element.classList.add(name);
    this._panelName = name;

    // For testing.
    WebInspector.panels[name] = this;

    this._shortcuts = /** !Object.<number, function(Event=):boolean> */ ({});
}

// Should by in sync with style declarations.
WebInspector.Panel.counterRightMargin = 25;

WebInspector.Panel.prototype = {
    get name()
    {
        return this._panelName;
    },

    /**
     * @return {?WebInspector.SearchableView}
     */
    searchableView: function()
    {
        return null;
    },

    /**
     * @override
     * @return {!Array.<!Element>}
     */
    elementsToRestoreScrollPositionsFor: function()
    {
        return [];
    },

    /**
     * @param {!KeyboardEvent} event
     */
    handleShortcut: function(event)
    {
        var shortcutKey = WebInspector.KeyboardShortcut.makeKeyFromEvent(event);
        var handler = this._shortcuts[shortcutKey];
        if (handler && handler(event))
            event.handled = true;
    },

    /**
     * @param {!Array.<!WebInspector.KeyboardShortcut.Descriptor>} keys
     * @param {function(!Event=):boolean} handler
     */
    registerShortcuts: function(keys, handler)
    {
        for (var i = 0; i < keys.length; ++i)
            this._shortcuts[keys[i].key] = handler;
    },

    /**
     * @param {!WebInspector.Infobar} infobar
     */
    showInfobar: function(infobar)
    {
        infobar.setCloseCallback(this._onInfobarClosed.bind(this, infobar));
        if (this.element.firstChild)
            this.element.insertBefore(infobar.element, this.element.firstChild);
        else
            this.element.appendChild(infobar.element);
        infobar.setParentView(this);
        this.doResize();
    },

    /**
     * @param {!WebInspector.Infobar} infobar
     */
    _onInfobarClosed: function(infobar)
    {
        infobar.element.remove();
        this.doResize();
    },

    __proto__: WebInspector.VBox.prototype
}

/**
 * @extends {WebInspector.Panel}
 * @param {string} name
 * @param {number=} defaultWidth
 * @constructor
 */
WebInspector.PanelWithSidebar = function(name, defaultWidth)
{
    WebInspector.Panel.call(this, name);

    this._panelSplitWidget = new WebInspector.SplitWidget(true, false, this._panelName + "PanelSplitViewState", defaultWidth || 200);
    this._panelSplitWidget.show(this.element);

    this._mainWidget = new WebInspector.VBox();
    this._panelSplitWidget.setMainWidget(this._mainWidget);

    this._sidebarWidget = new WebInspector.VBox();
    this._sidebarWidget.setMinimumSize(100, 25);
    this._panelSplitWidget.setSidebarWidget(this._sidebarWidget);

    this._sidebarWidget.element.classList.add("panel-sidebar");
}

WebInspector.PanelWithSidebar.prototype = {
    /**
     * @return {!Element}
     */
    panelSidebarElement: function()
    {
        return this._sidebarWidget.element;
    },

    /**
     * @return {!Element}
     */
    mainElement: function()
    {
        return this._mainWidget.element;
    },

    /**
     * @return {!WebInspector.SplitWidget}
     */
    splitWidget: function()
    {
        return this._panelSplitWidget;
    },

    __proto__: WebInspector.Panel.prototype
}
