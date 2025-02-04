// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.ntp.cards;

import android.content.Context;
import android.support.annotation.StringRes;

import org.chromium.chrome.R;

/**
 * Card that is shown when the user needs to be made aware of some information about their
 * configuration that affects the NTP suggestions.
 */
public abstract class StatusItem extends Leaf implements StatusCardViewHolder.DataSource {
    public static StatusItem createNoSuggestionsItem(SuggestionsCategoryInfo categoryInfo) {
        return new NoSuggestionsItem(categoryInfo);
    }

    private static class NoSuggestionsItem extends StatusItem {
        private final SuggestionsCategoryInfo mCategoryInfo;

        public NoSuggestionsItem(SuggestionsCategoryInfo info) {
            mCategoryInfo = info;
        }

        @Override
        @StringRes
        public int getHeader() {
            return R.string.ntp_status_card_title_no_suggestions;
        }

        @Override
        @StringRes
        public int getDescription() {
            return mCategoryInfo.getNoSuggestionDescription();
        }

        @Override
        @StringRes
        public int getActionLabel() {
            return 0;
        }

        @Override
        public void performAction(Context context) {
            assert false;
        }
    }

    @Override
    @ItemViewType
    protected int getItemViewType() {
        return ItemViewType.STATUS;
    }

    @Override
    protected void onBindViewHolder(NewTabPageViewHolder holder) {
        assert holder instanceof StatusCardViewHolder;
        ((StatusCardViewHolder) holder).onBindViewHolder(this);
    }
}
