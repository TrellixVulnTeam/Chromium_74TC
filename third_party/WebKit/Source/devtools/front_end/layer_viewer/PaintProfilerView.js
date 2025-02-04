/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @constructor
 * @param {function(string=)} showImageCallback
 * @extends {WebInspector.HBox}
 */
WebInspector.PaintProfilerView = function(showImageCallback)
{
    WebInspector.HBox.call(this, true);
    this.registerRequiredCSS("layer_viewer/paintProfiler.css");
    this.contentElement.classList.add("paint-profiler-overview");
    this._canvasContainer = this.contentElement.createChild("div", "paint-profiler-canvas-container");
    this._progressBanner = this.contentElement.createChild("div", "full-widget-dimmed-banner hidden");
    this._progressBanner.textContent = WebInspector.UIString("Profiling\u2026");
    this._pieChart = new WebInspector.PieChart(55, this._formatPieChartTime.bind(this), true);
    this._pieChart.element.classList.add("paint-profiler-pie-chart");
    this.contentElement.appendChild(this._pieChart.element);

    this._showImageCallback = showImageCallback;

    this._canvas = this._canvasContainer.createChild("canvas", "fill");
    this._context = this._canvas.getContext("2d");
    this._selectionWindow = new WebInspector.OverviewGrid.Window(this._canvasContainer);
    this._selectionWindow.addEventListener(WebInspector.OverviewGrid.Events.WindowChanged, this._onWindowChanged, this);

    this._innerBarWidth = 4 * window.devicePixelRatio;
    this._minBarHeight = window.devicePixelRatio;
    this._barPaddingWidth = 2 * window.devicePixelRatio;
    this._outerBarWidth = this._innerBarWidth + this._barPaddingWidth;
    this._pendingScale = 1;
    this._scale = this._pendingScale;

    this._reset();
}

/** @enum {symbol} */
WebInspector.PaintProfilerView.Events = {
    WindowChanged: Symbol("WindowChanged")
};

WebInspector.PaintProfilerView.prototype = {
    onResize: function()
    {
        this._update();
    },

    /**
     * @param {?WebInspector.PaintProfilerSnapshot} snapshot
     * @param {!Array.<!WebInspector.PaintProfilerLogItem>} log
     * @param {?DOMAgent.Rect} clipRect
     */
    setSnapshotAndLog: function(snapshot, log, clipRect)
    {
        this._reset();
        this._snapshot = snapshot;
        this._log = log;
        this._logCategories = this._log.map(WebInspector.PaintProfilerView._categoryForLogItem);

        if (!this._snapshot) {
            this._update();
            this._pieChart.setTotal(0);
            this._selectionWindow.setEnabled(false);
            return;
        }
        this._selectionWindow.setEnabled(true);
        this._progressBanner.classList.remove("hidden");
        this._updateImage();
        snapshot.profile(clipRect, onProfileDone.bind(this));
        /**
         * @param {!Array.<!LayerTreeAgent.PaintProfile>=} profiles
         * @this {WebInspector.PaintProfilerView}
         */
        function onProfileDone(profiles)
        {
            this._progressBanner.classList.add("hidden");
            this._profiles = profiles;
            this._update();
            this._updatePieChart();
        }
    },

    /**
     * @param {number} scale
     */
    setScale: function(scale)
    {
        var needsUpdate = scale > this._scale;
        var predictiveGrowthFactor = 2;
        this._pendingScale = Math.min(1, scale * predictiveGrowthFactor);
        if (needsUpdate)
            this._updateImage();
    },

    _update: function()
    {
        this._canvas.width = this._canvasContainer.clientWidth * window.devicePixelRatio;
        this._canvas.height = this._canvasContainer.clientHeight * window.devicePixelRatio;
        this._samplesPerBar = 0;
        if (!this._profiles || !this._profiles.length)
            return;

        var maxBars = Math.floor((this._canvas.width - 2 * this._barPaddingWidth) / this._outerBarWidth);
        var sampleCount = this._log.length;
        this._samplesPerBar = Math.ceil(sampleCount / maxBars);

        var maxBarTime = 0;
        var barTimes = [];
        var barHeightByCategory = [];
        var heightByCategory = {};
        for (var i = 0, lastBarIndex = 0, lastBarTime = 0; i < sampleCount;) {
            var categoryName = (this._logCategories[i] && this._logCategories[i].name) || "misc";
            var sampleIndex = this._log[i].commandIndex;
            for (var row = 0; row < this._profiles.length; row++) {
                var sample = this._profiles[row][sampleIndex];
                lastBarTime += sample;
                heightByCategory[categoryName] = (heightByCategory[categoryName] || 0) + sample;
            }
            ++i;
            if (i - lastBarIndex === this._samplesPerBar || i === sampleCount) {
                // Normalize by total number of samples accumulated.
                var factor = this._profiles.length * (i - lastBarIndex);
                lastBarTime /= factor;
                for (categoryName in heightByCategory)
                    heightByCategory[categoryName] /= factor;

                barTimes.push(lastBarTime);
                barHeightByCategory.push(heightByCategory);

                if (lastBarTime > maxBarTime)
                    maxBarTime = lastBarTime;
                lastBarTime = 0;
                heightByCategory = {};
                lastBarIndex = i;
            }
        }

        const paddingHeight = 4 * window.devicePixelRatio;
        var scale = (this._canvas.height - paddingHeight - this._minBarHeight) / maxBarTime;
        for (var i = 0; i < barTimes.length; ++i) {
            for (var categoryName in barHeightByCategory[i])
                barHeightByCategory[i][categoryName] *= (barTimes[i] * scale + this._minBarHeight) / barTimes[i];
            this._renderBar(i, barHeightByCategory[i]);
        }
    },

    /**
     * @param {number} index
     * @param {!Object.<string, number>} heightByCategory
     */
    _renderBar: function(index, heightByCategory)
    {
        var categories = WebInspector.PaintProfilerView.categories();
        var currentHeight = 0;
        var x = this._barPaddingWidth + index * this._outerBarWidth;
        for (var categoryName in categories) {
            if (!heightByCategory[categoryName])
                continue;
            currentHeight += heightByCategory[categoryName];
            var y = this._canvas.height - currentHeight;
            this._context.fillStyle = categories[categoryName].color;
            this._context.fillRect(x, y, this._innerBarWidth, heightByCategory[categoryName]);
        }
    },

    _onWindowChanged: function()
    {
        this.dispatchEventToListeners(WebInspector.PaintProfilerView.Events.WindowChanged);
        this._updatePieChart();
        if (this._updateImageTimer)
            return;
        this._updateImageTimer = setTimeout(this._updateImage.bind(this), 100);
    },

    _updatePieChart: function()
    {
        var window = this.selectionWindow();
        if (!this._profiles || !this._profiles.length || !window)
            return;
        var totalTime = 0;
        var timeByCategory = {};
        for (var i = window.left; i < window.right; ++i) {
            var logEntry = this._log[i];
            var category = WebInspector.PaintProfilerView._categoryForLogItem(logEntry);
            timeByCategory[category.color] = timeByCategory[category.color] || 0;
            for (var j = 0; j < this._profiles.length; ++j) {
                var time = this._profiles[j][logEntry.commandIndex];
                totalTime += time;
                timeByCategory[category.color] += time;
            }
        }
        this._pieChart.setTotal(totalTime / this._profiles.length);
        for (var color in timeByCategory)
            this._pieChart.addSlice(timeByCategory[color] / this._profiles.length, color);
    },

    /**
     * @param {number} value
     * @return {string}
     */
    _formatPieChartTime: function(value)
    {
        return Number.millisToString(value * 1000, true);
    },

    /**
     * @return {?{left: number, right: number}}
     */
    selectionWindow: function()
    {
        if (!this._log)
            return null;

        var screenLeft = this._selectionWindow.windowLeft * this._canvas.width;
        var screenRight = this._selectionWindow.windowRight * this._canvas.width;
        var barLeft = Math.floor(screenLeft / this._outerBarWidth);
        var barRight = Math.floor((screenRight + this._innerBarWidth - this._barPaddingWidth / 2) / this._outerBarWidth);
        var stepLeft = Number.constrain(barLeft * this._samplesPerBar, 0, this._log.length - 1);
        var stepRight = Number.constrain(barRight * this._samplesPerBar, 0, this._log.length);

        return { left: stepLeft, right: stepRight };
    },

    _updateImage: function()
    {
        delete this._updateImageTimer;
        var left = null;
        var right = null;
        var window = this.selectionWindow();
        if (this._profiles && this._profiles.length && window) {
            left = this._log[window.left].commandIndex;
            right = this._log[window.right - 1].commandIndex;
        }
        var scale = this._pendingScale;
        this._snapshot.requestImage(left, right, scale, image => { this._scale = scale; this._showImageCallback(image); });
    },

    _reset: function()
    {
        this._snapshot = null;
        this._profiles = null;
        this._selectionWindow.reset();
        this._selectionWindow.setEnabled(false);
    },

    __proto__: WebInspector.HBox.prototype
};

/**
 * @constructor
 * @extends {WebInspector.ThrottledWidget}
 */
WebInspector.PaintProfilerCommandLogView = function()
{
    WebInspector.ThrottledWidget.call(this);
    this.setMinimumSize(100, 25);
    this.element.classList.add("overflow-auto");

    this._treeOutline = new TreeOutlineInShadow();
    this.element.appendChild(this._treeOutline.element);

    this._log = [];
}

WebInspector.PaintProfilerCommandLogView.prototype = {
    /**
     * @param {?WebInspector.Target} target
     * @param {!Array.<!WebInspector.PaintProfilerLogItem>} log
     */
    setCommandLog: function(target, log)
    {
        this._target = target;
        this._log = log;
        /** @type {!Map<!WebInspector.PaintProfilerLogItem>} */
        this._treeItemCache = new Map();
        this.updateWindow({left: 0, right: this._log.length});
    },

    /**
      * @param {!WebInspector.PaintProfilerLogItem} logItem
      */
    _appendLogItem: function(logItem)
    {
        var treeElement = this._treeItemCache.get(logItem);
        if (!treeElement) {
            treeElement = new WebInspector.LogTreeElement(this, logItem);
            this._treeItemCache.set(logItem, treeElement);
        } else if (treeElement.parent) {
            return;
        }
        this._treeOutline.appendChild(treeElement);
    },

    /**
     * @param {?{left: number, right: number}} selectionWindow
     */
    updateWindow: function(selectionWindow)
    {
        this._selectionWindow = selectionWindow;
        this.update();
    },

    /**
     * @override
     * @return {!Promise<*>}
     */
    doUpdate: function()
    {
        if (!this._selectionWindow || !this._log.length) {
            this._treeOutline.removeChildren();
            return Promise.resolve();
        }
        var root = this._treeOutline.rootElement();
        for (;;) {
            var child = root.firstChild();
            if (!child || child._logItem.commandIndex >= this._selectionWindow.left)
                break;
            root.removeChildAtIndex(0);
        }
        for (;;) {
            var child = root.lastChild();
            if (!child || child._logItem.commandIndex < this._selectionWindow.right)
                break;
            root.removeChildAtIndex(root.children().length - 1);
        }
        for (var i = this._selectionWindow.left, right = this._selectionWindow.right; i < right; ++i)
            this._appendLogItem(this._log[i]);
        return Promise.resolve();
    },

    __proto__: WebInspector.ThrottledWidget.prototype
};

/**
  * @constructor
  * @param {!WebInspector.PaintProfilerCommandLogView} ownerView
  * @param {!WebInspector.PaintProfilerLogItem} logItem
  * @extends {TreeElement}
  */
WebInspector.LogTreeElement = function(ownerView, logItem)
{
    TreeElement.call(this, "", !!logItem.params);
    this._logItem = logItem;
    this._ownerView = ownerView;
    this._filled = false;
}

WebInspector.LogTreeElement.prototype = {
    onattach: function()
    {
        this._update();
    },

    onpopulate: function()
    {
        for (var param in this._logItem.params)
            WebInspector.LogPropertyTreeElement._appendLogPropertyItem(this, param, this._logItem.params[param]);
    },

    /**
      * @param {*} param
      * @param {string} name
      * @return {string}
      */
    _paramToString: function(param, name)
    {
        if (typeof param !== "object")
            return typeof param === "string" && param.length > 100 ? name : JSON.stringify(param);
        var str = "";
        var keyCount = 0;
        for (var key in param) {
            if (++keyCount > 4 || typeof param[key] === "object" || (typeof param[key] === "string" && param[key].length > 100))
                return name;
            if (str)
                str += ", ";
            str += param[key];
        }
        return str;
    },

    /**
      * @param {?Object<string, *>} params
      * @return {string}
      */
    _paramsToString: function(params)
    {
        var str = "";
        for (var key in params) {
            if (str)
                str += ", ";
            str += this._paramToString(params[key], key);
        }
        return str;
    },

    _update: function()
    {
        var title = createDocumentFragment();
        title.createTextChild(this._logItem.method + "(" + this._paramsToString(this._logItem.params) + ")");
        this.title = title;
    },

    __proto__: TreeElement.prototype
};

/**
  * @constructor
  * @param {!{name: string, value}} property
  * @extends {TreeElement}
  */
WebInspector.LogPropertyTreeElement = function(property)
{
    TreeElement.call(this);
    this._property = property;
};

/**
  * @param {!TreeElement} element
  * @param {string} name
  * @param {*} value
  */
WebInspector.LogPropertyTreeElement._appendLogPropertyItem = function(element, name, value)
{
    var treeElement = new WebInspector.LogPropertyTreeElement({name: name, value: value});
    element.appendChild(treeElement);
    if (value && typeof value === "object") {
        for (var property in value)
            WebInspector.LogPropertyTreeElement._appendLogPropertyItem(treeElement, property, value[property]);
    }
};

WebInspector.LogPropertyTreeElement.prototype = {
    onattach: function()
    {
        var title = createDocumentFragment();
        var nameElement = title.createChild("span", "name");
        nameElement.textContent = this._property.name;
        var separatorElement = title.createChild("span", "separator");
        separatorElement.textContent = ": ";
        if (this._property.value === null || typeof this._property.value !== "object") {
            var valueElement = title.createChild("span", "value");
            valueElement.textContent = JSON.stringify(this._property.value);
            valueElement.classList.add("cm-js-" + (this._property.value === null ? "null" : typeof this._property.value));
        }
        this.title = title;
    },

    __proto__: TreeElement.prototype
}

/**
 * @return {!Object.<string, !WebInspector.PaintProfilerCategory>}
 */
WebInspector.PaintProfilerView.categories = function()
{
    if (WebInspector.PaintProfilerView._categories)
        return WebInspector.PaintProfilerView._categories;
    WebInspector.PaintProfilerView._categories = {
        shapes: new WebInspector.PaintProfilerCategory("shapes", WebInspector.UIString("Shapes"), "rgb(255, 161, 129)"),
        bitmap: new WebInspector.PaintProfilerCategory("bitmap", WebInspector.UIString("Bitmap"), "rgb(136, 196, 255)"),
        text: new WebInspector.PaintProfilerCategory("text", WebInspector.UIString("Text"), "rgb(180, 255, 137)"),
        misc: new WebInspector.PaintProfilerCategory("misc", WebInspector.UIString("Misc"), "rgb(206, 160, 255)")
    };
    return WebInspector.PaintProfilerView._categories;
};

/**
 * @constructor
 * @param {string} name
 * @param {string} title
 * @param {string} color
 */
WebInspector.PaintProfilerCategory = function(name, title, color)
{
    this.name = name;
    this.title = title;
    this.color = color;
}

/**
 * @return {!Object.<string, !WebInspector.PaintProfilerCategory>}
 */
WebInspector.PaintProfilerView._initLogItemCategories = function()
{
    if (WebInspector.PaintProfilerView._logItemCategoriesMap)
        return WebInspector.PaintProfilerView._logItemCategoriesMap;

    var categories = WebInspector.PaintProfilerView.categories();

    var logItemCategories = {};
    logItemCategories["Clear"] = categories["misc"];
    logItemCategories["DrawPaint"] = categories["misc"];
    logItemCategories["DrawData"] = categories["misc"];
    logItemCategories["SetMatrix"] = categories["misc"];
    logItemCategories["PushCull"] = categories["misc"];
    logItemCategories["PopCull"] = categories["misc"];
    logItemCategories["Translate"] = categories["misc"];
    logItemCategories["Scale"] = categories["misc"];
    logItemCategories["Concat"] = categories["misc"];
    logItemCategories["Restore"] = categories["misc"];
    logItemCategories["SaveLayer"] = categories["misc"];
    logItemCategories["Save"] = categories["misc"];
    logItemCategories["BeginCommentGroup"] = categories["misc"];
    logItemCategories["AddComment"] = categories["misc"];
    logItemCategories["EndCommentGroup"] = categories["misc"];
    logItemCategories["ClipRect"] = categories["misc"];
    logItemCategories["ClipRRect"] = categories["misc"];
    logItemCategories["ClipPath"] = categories["misc"];
    logItemCategories["ClipRegion"] = categories["misc"];
    logItemCategories["DrawPoints"] = categories["shapes"];
    logItemCategories["DrawRect"] = categories["shapes"];
    logItemCategories["DrawOval"] = categories["shapes"];
    logItemCategories["DrawRRect"] = categories["shapes"];
    logItemCategories["DrawPath"] = categories["shapes"];
    logItemCategories["DrawVertices"] = categories["shapes"];
    logItemCategories["DrawDRRect"] = categories["shapes"];
    logItemCategories["DrawBitmap"] = categories["bitmap"];
    logItemCategories["DrawBitmapRectToRect"] = categories["bitmap"];
    logItemCategories["DrawBitmapMatrix"] = categories["bitmap"];
    logItemCategories["DrawBitmapNine"] = categories["bitmap"];
    logItemCategories["DrawSprite"] = categories["bitmap"];
    logItemCategories["DrawPicture"] = categories["bitmap"];
    logItemCategories["DrawText"] = categories["text"];
    logItemCategories["DrawPosText"] = categories["text"];
    logItemCategories["DrawPosTextH"] = categories["text"];
    logItemCategories["DrawTextOnPath"] = categories["text"];

    WebInspector.PaintProfilerView._logItemCategoriesMap = logItemCategories;
    return logItemCategories;
}

/**
 * @param {!Object} logItem
 * @return {!WebInspector.PaintProfilerCategory}
 */
WebInspector.PaintProfilerView._categoryForLogItem = function(logItem)
{
    var method = logItem.method.toTitleCase();

    var logItemCategories = WebInspector.PaintProfilerView._initLogItemCategories();
    var result = logItemCategories[method];
    if (!result) {
        result = WebInspector.PaintProfilerView.categories()["misc"];
        logItemCategories[method] = result;
    }
    return result;
}
