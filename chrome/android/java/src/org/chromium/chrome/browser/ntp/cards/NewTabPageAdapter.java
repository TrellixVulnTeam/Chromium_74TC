// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.ntp.cards;

import android.annotation.SuppressLint;
import android.graphics.Canvas;
import android.support.v7.widget.RecyclerView;
import android.support.v7.widget.RecyclerView.Adapter;
import android.support.v7.widget.RecyclerView.ViewHolder;
import android.support.v7.widget.helper.ItemTouchHelper;
import android.view.View;
import android.view.ViewGroup;

import org.chromium.base.Log;
import org.chromium.base.VisibleForTesting;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ntp.NewTabPageView.NewTabPageManager;
import org.chromium.chrome.browser.ntp.UiConfig;
import org.chromium.chrome.browser.ntp.snippets.CategoryInt;
import org.chromium.chrome.browser.ntp.snippets.CategoryStatus;
import org.chromium.chrome.browser.ntp.snippets.CategoryStatus.CategoryStatusEnum;
import org.chromium.chrome.browser.ntp.snippets.SectionHeaderViewHolder;
import org.chromium.chrome.browser.ntp.snippets.SnippetArticle;
import org.chromium.chrome.browser.ntp.snippets.SnippetArticleViewHolder;
import org.chromium.chrome.browser.ntp.snippets.SnippetsBridge;
import org.chromium.chrome.browser.ntp.snippets.SuggestionsSource;
import org.chromium.chrome.browser.signin.SigninManager.SignInStateObserver;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * A class that handles merging above the fold elements and below the fold cards into an adapter
 * that will be used to back the NTP RecyclerView. The first element in the adapter should always be
 * the above-the-fold view (containing the logo, search box, and most visited tiles) and subsequent
 * elements will be the cards shown to the user
 */
public class NewTabPageAdapter
        extends Adapter<NewTabPageViewHolder> implements SuggestionsSource.Observer, NodeParent {
    private static final String TAG = "Ntp";

    private final NewTabPageManager mNewTabPageManager;
    private final View mAboveTheFoldView;
    private final UiConfig mUiConfig;
    private final ItemTouchCallbacks mItemTouchCallbacks = new ItemTouchCallbacks();
    private NewTabPageRecyclerView mRecyclerView;

    /**
     * List of all item groups (which can themselves contain multiple items. When flattened, this
     * will be a list of all items the adapter exposes.
     */
    private final List<TreeNode> mGroups = new ArrayList<>();
    private final AboveTheFoldItem mAboveTheFold = new AboveTheFoldItem();
    private final SignInPromo mSigninPromo;
    private final AllDismissedItem mAllDismissed = new AllDismissedItem();
    private final Footer mFooter = new Footer();
    private final SpacingItem mBottomSpacer = new SpacingItem();
    private final InnerNode mRoot;

    /** Maps suggestion categories to sections, with stable iteration ordering. */
    private final Map<Integer, SuggestionsSection> mSections = new LinkedHashMap<>();

    private class ItemTouchCallbacks extends ItemTouchHelper.Callback {
        @Override
        public void onSwiped(ViewHolder viewHolder, int direction) {
            mRecyclerView.onItemDismissStarted(viewHolder);
            NewTabPageAdapter.this.dismissItem(viewHolder.getAdapterPosition());
        }

        @Override
        public void clearView(RecyclerView recyclerView, ViewHolder viewHolder) {
            // clearView() is called when an interaction with the item is finished, which does
            // not mean that the user went all the way and dismissed the item before releasing it.
            // We need to check that the item has been removed.
            if (viewHolder.getAdapterPosition() == RecyclerView.NO_POSITION) {
                mRecyclerView.onItemDismissFinished(viewHolder);
            }

            super.clearView(recyclerView, viewHolder);
        }

        @Override
        public boolean onMove(RecyclerView recyclerView, ViewHolder viewHolder, ViewHolder target) {
            assert false; // Drag and drop not supported, the method will never be called.
            return false;
        }

        @Override
        public int getMovementFlags(RecyclerView recyclerView, ViewHolder viewHolder) {
            assert viewHolder instanceof NewTabPageViewHolder;

            int swipeFlags = 0;
            if (((NewTabPageViewHolder) viewHolder).isDismissable()) {
                swipeFlags = ItemTouchHelper.START | ItemTouchHelper.END;
            }

            return makeMovementFlags(0 /* dragFlags */, swipeFlags);
        }

        @Override
        public void onChildDraw(Canvas c, RecyclerView recyclerView, ViewHolder viewHolder,
                float dX, float dY, int actionState, boolean isCurrentlyActive) {
            assert viewHolder instanceof NewTabPageViewHolder;

            // The item has already been removed. We have nothing more to do.
            if (viewHolder.getAdapterPosition() == RecyclerView.NO_POSITION) return;

            // We use our own implementation of the dismissal animation, so we don't call the
            // parent implementation. (by default it changes the translation-X and elevation)
            mRecyclerView.updateViewStateForDismiss(dX, viewHolder);

            // If there is another item that should be animated at the same time, do the same to it.
            ViewHolder siblingViewHolder = getDismissSibling(viewHolder);
            if (siblingViewHolder != null) {
                mRecyclerView.updateViewStateForDismiss(dX, siblingViewHolder);
            }
        }
    }

    /**
     * Creates the adapter that will manage all the cards to display on the NTP.
     *
     * @param manager the NewTabPageManager to use to interact with the rest of the system.
     * @param aboveTheFoldView the layout encapsulating all the above-the-fold elements
     *                         (logo, search box, most visited tiles)
     * @param uiConfig the NTP UI configuration, to be passed to created views.
     */
    public NewTabPageAdapter(NewTabPageManager manager, View aboveTheFoldView, UiConfig uiConfig) {
        mNewTabPageManager = manager;
        mAboveTheFoldView = aboveTheFoldView;
        mUiConfig = uiConfig;
        mRoot = new InnerNode(this) {
            @Override
            protected List<TreeNode> getChildren() {
                return mGroups;
            }
        };

        mSigninPromo = new SignInPromo(mRoot);
        resetSections(/*alwaysAllowEmptySections=*/false);
        mNewTabPageManager.getSuggestionsSource().setObserver(this);

        mNewTabPageManager.registerSignInStateObserver(new SignInStateObserver() {
            @Override
            public void onSignedIn() {
                mSigninPromo.hide();
                resetSections(/*alwaysAllowEmptySections=*/false);
            }

            @Override
            public void onSignedOut() {
                mSigninPromo.maybeShow();
            }
        });
    }

    /**
     * Resets the sections, reloading the whole new tab page content.
     * @param alwaysAllowEmptySections Whether sections are always allowed to be displayed when
     *     they are empty, even when they are normally not.
     */
    public void resetSections(boolean alwaysAllowEmptySections) {
        mSections.clear();

        SuggestionsSource suggestionsSource = mNewTabPageManager.getSuggestionsSource();
        int[] categories = suggestionsSource.getCategories();
        int[] suggestionsPerCategory = new int[categories.length];
        int i = 0;
        for (int category : categories) {
            int categoryStatus = suggestionsSource.getCategoryStatus(category);
            assert categoryStatus != CategoryStatus.NOT_PROVIDED;
            if (categoryStatus == CategoryStatus.LOADING_ERROR
                    || categoryStatus == CategoryStatus.CATEGORY_EXPLICITLY_DISABLED)
                continue;

            suggestionsPerCategory[i++] =
                    resetSection(category, categoryStatus, alwaysAllowEmptySections);
        }

        mNewTabPageManager.trackSnippetsPageImpression(categories, suggestionsPerCategory);

        updateGroups();
    }

    /**
     * Resets the section for {@code category}. Removes the section if there are no suggestions for
     * it and it is not allowed to be empty. Otherwise, creates the section if it is not present
     * yet. Sets the available suggestions on the section.
     * @param category The category for which the section must be reset.
     * @param categoryStatus The category status.
     * @param alwaysAllowEmptySections Whether sections are always allowed to be displayed when
     *     they are empty, even when they are normally not.
     * @return The number of suggestions for the section.
     */
    private int resetSection(@CategoryInt int category, @CategoryStatusEnum int categoryStatus,
            boolean alwaysAllowEmptySections) {
        SuggestionsSource suggestionsSource = mNewTabPageManager.getSuggestionsSource();
        List<SnippetArticle> suggestions = suggestionsSource.getSuggestionsForCategory(category);
        SuggestionsCategoryInfo info = suggestionsSource.getCategoryInfo(category);

        // Do not show an empty section if not allowed.
        if (suggestions.isEmpty() && !info.showIfEmpty() && !alwaysAllowEmptySections) {
            mSections.remove(category);
            return 0;
        }

        // Create the section if needed.
        SuggestionsSection section = mSections.get(category);
        if (section == null) {
            section = new SuggestionsSection(mRoot, info);
            mSections.put(category, section);
        }

        // Add the new suggestions.
        setSuggestions(category, suggestions, categoryStatus);

        return suggestions.size();
    }

    /** Returns callbacks to configure the interactions with the RecyclerView's items. */
    public ItemTouchHelper.Callback getItemTouchCallbacks() {
        return mItemTouchCallbacks;
    }

    @Override
    public void onNewSuggestions(@CategoryInt int category) {
        // We never want to add suggestions from unknown categories.
        if (!mSections.containsKey(category)) return;

        // We never want to refresh the suggestions if we already have some content.
        if (mSections.get(category).hasSuggestions()) return;

        // The status may have changed while the suggestions were loading, perhaps they should not
        // be displayed any more.
        @CategoryStatusEnum
        int status = mNewTabPageManager.getSuggestionsSource().getCategoryStatus(category);
        if (!SnippetsBridge.isCategoryEnabled(status)) {
            Log.w(TAG, "Received suggestions for a disabled category (id=%d, status=%d)", category,
                    status);
            return;
        }

        List<SnippetArticle> suggestions =
                mNewTabPageManager.getSuggestionsSource().getSuggestionsForCategory(category);

        Log.d(TAG, "Received %d new suggestions for category %d.", suggestions.size(), category);

        // At first, there might be no suggestions available, we wait until they have been fetched.
        if (suggestions.isEmpty()) return;

        setSuggestions(category, suggestions, status);
    }

    @Override
    public void onCategoryStatusChanged(@CategoryInt int category, @CategoryStatusEnum int status) {
        // Observers should not be registered for this state.
        assert status != CategoryStatus.ALL_SUGGESTIONS_EXPLICITLY_DISABLED;

        // If there is no section for this category there is nothing to do.
        if (!mSections.containsKey(category)) return;

        switch (status) {
            case CategoryStatus.NOT_PROVIDED:
                // The section provider has gone away. Keep open UIs as they are.
                return;

            case CategoryStatus.CATEGORY_EXPLICITLY_DISABLED:
            case CategoryStatus.LOADING_ERROR:
                // Need to remove the entire section from the UI immediately.
                removeSection(mSections.get(category));
                return;

            case CategoryStatus.SIGNED_OUT:
                resetSection(category, status, /*alwaysAllowEmptySections=*/false);
                return;

            default:
                mSections.get(category).setStatus(status);
                return;
        }
    }

    @Override
    public void onSuggestionInvalidated(@CategoryInt int category, String idWithinCategory) {
        if (!mSections.containsKey(category)) return;
        mSections.get(category).removeSuggestionById(idWithinCategory);
    }

    @Override
    @ItemViewType
    public int getItemViewType(int position) {
        return mRoot.getItemViewType(position);
    }

    @Override
    public NewTabPageViewHolder onCreateViewHolder(ViewGroup parent, int viewType) {
        assert parent == mRecyclerView;

        switch (viewType) {
            case ItemViewType.ABOVE_THE_FOLD:
                return new NewTabPageViewHolder(mAboveTheFoldView);

            case ItemViewType.HEADER:
                return new SectionHeaderViewHolder(mRecyclerView, mUiConfig);

            case ItemViewType.SNIPPET:
                return new SnippetArticleViewHolder(mRecyclerView, mNewTabPageManager, mUiConfig);

            case ItemViewType.SPACING:
                return new NewTabPageViewHolder(SpacingItem.createView(parent));

            case ItemViewType.STATUS:
                return new StatusCardViewHolder(mRecyclerView, mUiConfig);

            case ItemViewType.PROGRESS:
                return new ProgressViewHolder(mRecyclerView);

            case ItemViewType.ACTION:
                return new ActionItem.ViewHolder(mRecyclerView, mNewTabPageManager, mUiConfig);

            case ItemViewType.PROMO:
                return new SignInPromo.ViewHolder(mRecyclerView, mUiConfig);

            case ItemViewType.FOOTER:
                return new Footer.ViewHolder(mRecyclerView, mNewTabPageManager);

            case ItemViewType.ALL_DISMISSED:
                return new AllDismissedItem.ViewHolder(mRecyclerView, mNewTabPageManager, this);
        }

        assert false : viewType;
        return null;
    }

    @Override
    public void onBindViewHolder(NewTabPageViewHolder holder, final int position) {
        mRoot.onBindViewHolder(holder, position);
    }

    @Override
    public int getItemCount() {
        return mRoot.getItemCount();
    }

    public int getAboveTheFoldPosition() {
        return getGroupPositionOffset(mAboveTheFold);
    }

    public int getFirstHeaderPosition() {
        int count = getItemCount();
        for (int i = 0; i < count; i++) {
            if (getItemViewType(i) == ItemViewType.HEADER) return i;
        }
        return RecyclerView.NO_POSITION;
    }

    public int getFirstCardPosition() {
        for (int i = 0; i < getItemCount(); ++i) {
            if (CardViewHolder.isCard(getItemViewType(i))) return i;
        }
        return RecyclerView.NO_POSITION;
    }

    public int getFooterPosition() {
        return getGroupPositionOffset(mFooter);
    }

    public int getBottomSpacerPosition() {
        return getGroupPositionOffset(mBottomSpacer);
    }

    public int getLastContentItemPosition() {
        return getGroupPositionOffset(hasAllBeenDismissed() ? mAllDismissed : mFooter);
    }

    public int getSuggestionPosition(SnippetArticle article) {
        for (int i = 0; i < mRoot.getItemCount(); i++) {
            SnippetArticle articleToCheck = mRoot.getSuggestionAt(i);
            if (articleToCheck != null && articleToCheck.equals(article)) return i;
        }
        return RecyclerView.NO_POSITION;
    }

    /** Start a request for new snippets. */
    public void reloadSnippets() {
        SnippetsBridge.fetchSnippets(/*forceRequest=*/true);
    }

    private void setSuggestions(@CategoryInt int category, List<SnippetArticle> suggestions,
            @CategoryStatusEnum int status) {
        // Count the number of suggestions before this category.
        int globalPositionOffset = 0;
        for (Map.Entry<Integer, SuggestionsSection> entry : mSections.entrySet()) {
            if (entry.getKey() == category) break;
            globalPositionOffset += entry.getValue().getSuggestionsCount();
        }
        // Assign global indices to the new suggestions.
        for (SnippetArticle suggestion : suggestions) {
            suggestion.mGlobalPosition = globalPositionOffset + suggestion.mPosition;
        }

        mSections.get(category).setSuggestions(suggestions, status);
    }

    private void updateGroups() {
        mGroups.clear();
        mGroups.add(mAboveTheFold);
        mGroups.addAll(mSections.values());
        mGroups.add(mSigninPromo);
        mGroups.add(hasAllBeenDismissed() ? mAllDismissed : mFooter);
        mGroups.add(mBottomSpacer);

        // TODO(mvanouwerkerk): Notify about the subset of changed items. At least |mAboveTheFold|
        // has not changed when refreshing from the all dismissed state.
        notifyDataSetChanged();
    }

    private void removeSection(SuggestionsSection section) {
        mSections.remove(section.getCategory());
        int startPos = getGroupPositionOffset(section);
        mGroups.remove(section);
        notifyItemRangeRemoved(startPos, section.getItemCount());

        if (hasAllBeenDismissed()) {
            int footerPosition = getFooterPosition();
            mGroups.set(mGroups.indexOf(mFooter), mAllDismissed);
            notifyItemChanged(footerPosition);
        }

        notifyItemChanged(getBottomSpacerPosition());
    }

    @Override
    public void onItemRangeChanged(TreeNode child, int itemPosition, int itemCount) {
        assert child == mRoot;
        if (mGroups.isEmpty()) return; // The sections have not been initialised yet.
        notifyItemRangeChanged(itemPosition, itemCount);
    }

    @Override
    public void onItemRangeInserted(TreeNode child, int itemPosition, int itemCount) {
        assert child == mRoot;
        if (mGroups.isEmpty()) return; // The sections have not been initialised yet.
        notifyItemRangeInserted(itemPosition, itemCount);
        notifyItemChanged(getItemCount() - 1); // Refresh the spacer too.
    }

    @Override
    public void onItemRangeRemoved(TreeNode child, int itemPosition, int itemCount) {
        assert child == mRoot;
        if (mGroups.isEmpty()) return; // The sections have not been initialised yet.
        notifyItemRangeRemoved(itemPosition, itemCount);
        notifyItemChanged(getItemCount() - 1); // Refresh the spacer too.
    }

    @Override
    public void onAttachedToRecyclerView(RecyclerView recyclerView) {
        super.onAttachedToRecyclerView(recyclerView);

        // We are assuming for now that the adapter is used with a single RecyclerView.
        // Getting the reference as we are doing here is going to be broken if that changes.
        assert mRecyclerView == null;

        // FindBugs chokes on the cast below when not checked, raising BC_UNCONFIRMED_CAST
        assert recyclerView instanceof NewTabPageRecyclerView;

        mRecyclerView = (NewTabPageRecyclerView) recyclerView;
    }

    /**
     * Dismisses the item at the provided adapter position. Can also cause the dismissal of other
     * items or even entire sections.
     */
    // TODO(crbug.com/635567): Fix this properly.
    @SuppressLint("SwitchIntDef")
    public void dismissItem(int position) {
        int itemViewType = getItemViewType(position);

        // TODO(dgn): Polymorphism is supposed to allow to avoid that kind of stuff.
        switch (itemViewType) {
            case ItemViewType.STATUS:
            case ItemViewType.ACTION:
                dismissSection(getSuggestionsSection(position));
                return;

            case ItemViewType.SNIPPET:
                dismissSuggestion(position);
                return;

            case ItemViewType.PROMO:
                dismissPromo();
                return;

            default:
                Log.wtf(TAG, "Unsupported dismissal of item of type %d", itemViewType);
                return;
        }
    }

    private void dismissSection(SuggestionsSection section) {
        mNewTabPageManager.getSuggestionsSource().dismissCategory(section.getCategory());
        removeSection(section);
    }

    private void dismissSuggestion(int position) {
        SnippetArticle suggestion = mRoot.getSuggestionAt(position);
        SuggestionsSource suggestionsSource = mNewTabPageManager.getSuggestionsSource();
        if (suggestionsSource == null) {
            // It is possible for this method to be called after the NewTabPage has had destroy()
            // called. This can happen when NewTabPageRecyclerView.dismissWithAnimation() is called
            // and the animation ends after the user has navigated away. In this case we cannot
            // inform the native side that the snippet has been dismissed (http://crbug.com/649299).
            return;
        }

        announceItemRemoved(suggestion.mTitle);

        suggestionsSource.dismissSuggestion(suggestion);
        SuggestionsSection section = getSuggestionsSection(position);
        section.removeSuggestion(suggestion);
    }

    private void dismissPromo() {
        // TODO(dgn): accessibility announcement.
        mSigninPromo.dismiss();

        if (hasAllBeenDismissed()) {
            int footerPosition = getFooterPosition();
            mGroups.set(mGroups.indexOf(mFooter), mAllDismissed);
            notifyItemChanged(footerPosition);
        }
    }

    /**
     * Returns another view holder that should be dismissed at the same time as the provided one.
     */
    public ViewHolder getDismissSibling(ViewHolder viewHolder) {
        int swipePos = viewHolder.getAdapterPosition();
        SuggestionsSection section = getSuggestionsSection(swipePos);
        if (section == null) return null;

        int siblingPosDelta =
                section.getDismissSiblingPosDelta(swipePos - getGroupPositionOffset(section));
        if (siblingPosDelta == 0) return null;

        return mRecyclerView.findViewHolderForAdapterPosition(siblingPosDelta + swipePos);
    }

    private boolean hasAllBeenDismissed() {
        return mSections.isEmpty() && !mSigninPromo.isShown();
    }

    /**
     * @param itemPosition The position of an item in the adapter.
     * @return Returns the {@link SuggestionsSection} that contains the item at
     *     {@code itemPosition}, or null if the item is not part of one.
     */
    @VisibleForTesting
    SuggestionsSection getSuggestionsSection(int itemPosition) {
        TreeNode child = mGroups.get(mRoot.getChildIndexForPosition(itemPosition));
        if (!(child instanceof SuggestionsSection)) return null;
        return (SuggestionsSection) child;
    }

    @VisibleForTesting
    List<TreeNode> getGroups() {
        return Collections.unmodifiableList(mGroups);
    }

    @VisibleForTesting
    int getGroupPositionOffset(TreeNode group) {
        return mRoot.getStartingOffsetForChild(group);
    }

    @VisibleForTesting
    SnippetArticle getSuggestionAt(int position) {
        return mRoot.getSuggestionAt(position);
    }

    private void announceItemRemoved(String suggestionTitle) {
        // In tests the RecyclerView can be null.
        if (mRecyclerView == null) return;

        mRecyclerView.announceForAccessibility(mRecyclerView.getResources().getString(
                R.string.ntp_accessibility_item_removed, suggestionTitle));
    }
}
