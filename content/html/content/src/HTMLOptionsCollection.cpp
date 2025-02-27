/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/HTMLOptionsCollection.h"

#include "HTMLOptGroupElement.h"
#include "mozAutoDocUpdate.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLOptionElement.h"
#include "mozilla/dom/HTMLOptionsCollectionBinding.h"
#include "mozilla/dom/HTMLSelectElement.h"
#include "mozilla/Util.h"
#include "nsContentCreatorFunctions.h"
#include "nsError.h"
#include "nsEventDispatcher.h"
#include "nsEventStates.h"
#include "nsFormSubmission.h"
#include "nsGkAtoms.h"
#include "nsGUIEvent.h"
#include "nsIComboboxControlFrame.h"
#include "nsIDocument.h"
#include "nsIDOMHTMLOptGroupElement.h"
#include "nsIFormControlFrame.h"
#include "nsIForm.h"
#include "nsIFormProcessor.h"
#include "nsIFrame.h"
#include "nsIListControlFrame.h"
#include "nsLayoutUtils.h"
#include "nsMappedAttributes.h"
#include "nsRuleData.h"
#include "nsServiceManagerUtils.h"
#include "nsStyleConsts.h"

DOMCI_DATA(HTMLOptionsCollection, mozilla::dom::HTMLOptionsCollection)

namespace mozilla {
namespace dom {

HTMLOptionsCollection::HTMLOptionsCollection(HTMLSelectElement* aSelect)
{
  SetIsDOMBinding();

  // Do not maintain a reference counted reference. When
  // the select goes away, it will let us know.
  mSelect = aSelect;
}

HTMLOptionsCollection::~HTMLOptionsCollection()
{
  DropReference();
}

void
HTMLOptionsCollection::DropReference()
{
  // Drop our (non ref-counted) reference
  mSelect = nullptr;
}

nsresult
HTMLOptionsCollection::GetOptionIndex(Element* aOption,
                                      int32_t aStartIndex,
                                      bool aForward,
                                      int32_t* aIndex)
{
  // NOTE: aIndex shouldn't be set if the returned value isn't NS_OK.

  int32_t index;

  // Make the common case fast
  if (aStartIndex == 0 && aForward) {
    index = mElements.IndexOf(aOption);
    if (index == -1) {
      return NS_ERROR_FAILURE;
    }
    
    *aIndex = index;
    return NS_OK;
  }

  int32_t high = mElements.Length();
  int32_t step = aForward ? 1 : -1;

  for (index = aStartIndex; index < high && index > -1; index += step) {
    if (mElements[index] == aOption) {
      *aIndex = index;
      return NS_OK;
    }
  }

  return NS_ERROR_FAILURE;
}


NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_1(HTMLOptionsCollection, mElements)

// nsISupports

// QueryInterface implementation for HTMLOptionsCollection
NS_INTERFACE_TABLE_HEAD(HTMLOptionsCollection)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_TABLE3(HTMLOptionsCollection,
                      nsIHTMLCollection,
                      nsIDOMHTMLOptionsCollection,
                      nsIDOMHTMLCollection)
  NS_INTERFACE_TABLE_TO_MAP_SEGUE_CYCLE_COLLECTION(HTMLOptionsCollection)
NS_INTERFACE_MAP_END


NS_IMPL_CYCLE_COLLECTING_ADDREF(HTMLOptionsCollection)
NS_IMPL_CYCLE_COLLECTING_RELEASE(HTMLOptionsCollection)


JSObject*
HTMLOptionsCollection::WrapObject(JSContext* aCx, JSObject* aScope)
{
  return HTMLOptionsCollectionBinding::Wrap(aCx, aScope, this);
}

NS_IMETHODIMP
HTMLOptionsCollection::GetLength(uint32_t* aLength)
{
  *aLength = mElements.Length();

  return NS_OK;
}

NS_IMETHODIMP
HTMLOptionsCollection::SetLength(uint32_t aLength)
{
  if (!mSelect) {
    return NS_ERROR_UNEXPECTED;
  }

  return mSelect->SetLength(aLength);
}

NS_IMETHODIMP
HTMLOptionsCollection::SetOption(uint32_t aIndex,
                                 nsIDOMHTMLOptionElement* aOption)
{
  if (!mSelect) {
    return NS_OK;
  }

  // if the new option is null, just remove this option.  Note that it's safe
  // to pass a too-large aIndex in here.
  if (!aOption) {
    mSelect->Remove(aIndex);

    // We're done.
    return NS_OK;
  }

  nsresult rv = NS_OK;

  uint32_t index = uint32_t(aIndex);

  // Now we're going to be setting an option in our collection
  if (index > mElements.Length()) {
    // Fill our array with blank options up to (but not including, since we're
    // about to change it) aIndex, for compat with other browsers.
    rv = SetLength(index);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  NS_ASSERTION(index <= mElements.Length(), "SetLength lied");
  
  nsCOMPtr<nsIDOMNode> ret;
  if (index == mElements.Length()) {
    rv = mSelect->AppendChild(aOption, getter_AddRefs(ret));
  } else {
    // Find the option they're talking about and replace it
    // hold a strong reference to follow COM rules.
    nsCOMPtr<nsIDOMHTMLOptionElement> refChild = ItemAsOption(index);
    NS_ENSURE_TRUE(refChild, NS_ERROR_UNEXPECTED);

    nsCOMPtr<nsIDOMNode> parent;
    refChild->GetParentNode(getter_AddRefs(parent));
    if (parent) {
      rv = parent->ReplaceChild(aOption, refChild, getter_AddRefs(ret));
    }
  }

  return rv;
}

int32_t
HTMLOptionsCollection::GetSelectedIndex(ErrorResult& aError)
{
  if (!mSelect) {
    aError.Throw(NS_ERROR_UNEXPECTED);
    return 0;
  }

  int32_t selectedIndex;
  aError = mSelect->GetSelectedIndex(&selectedIndex);
  return selectedIndex;
}

NS_IMETHODIMP
HTMLOptionsCollection::GetSelectedIndex(int32_t* aSelectedIndex)
{
  ErrorResult rv;
  *aSelectedIndex = GetSelectedIndex(rv);
  return rv.ErrorCode();
}

void
HTMLOptionsCollection::SetSelectedIndex(int32_t aSelectedIndex,
                                        ErrorResult& aError)
{
  if (!mSelect) {
    aError.Throw(NS_ERROR_UNEXPECTED);
    return;
  }

  aError = mSelect->SetSelectedIndex(aSelectedIndex);
}

NS_IMETHODIMP
HTMLOptionsCollection::SetSelectedIndex(int32_t aSelectedIndex)
{
  ErrorResult rv;
  SetSelectedIndex(aSelectedIndex, rv);
  return rv.ErrorCode();
}

NS_IMETHODIMP
HTMLOptionsCollection::Item(uint32_t aIndex, nsIDOMNode** aReturn)
{
  nsISupports* item = GetElementAt(aIndex);
  if (!item) {
    *aReturn = nullptr;

    return NS_OK;
  }

  return CallQueryInterface(item, aReturn);
}

Element*
HTMLOptionsCollection::GetElementAt(uint32_t aIndex)
{
  return ItemAsOption(aIndex);
}

static HTMLOptionElement*
GetNamedItemHelper(nsTArray<nsRefPtr<HTMLOptionElement> > &aElements,
                   const nsAString& aName)
{
  uint32_t count = aElements.Length();
  for (uint32_t i = 0; i < count; i++) {
    HTMLOptionElement* content = aElements.ElementAt(i);
    if (content &&
        (content->AttrValueIs(kNameSpaceID_None, nsGkAtoms::name, aName,
                              eCaseMatters) ||
         content->AttrValueIs(kNameSpaceID_None, nsGkAtoms::id, aName,
                              eCaseMatters))) {
      return content;
    }
  }

  return nullptr;
}

nsINode*
HTMLOptionsCollection::GetParentObject()
{
  return mSelect;
}

NS_IMETHODIMP
HTMLOptionsCollection::NamedItem(const nsAString& aName,
                                 nsIDOMNode** aReturn)
{
  NS_IF_ADDREF(*aReturn = GetNamedItemHelper(mElements, aName));

  return NS_OK;
}

JSObject*
HTMLOptionsCollection::NamedItem(JSContext* cx, const nsAString& name,
                                 ErrorResult& error)
{
  nsINode* item = GetNamedItemHelper(mElements, name);
  if (!item) {
    return nullptr;
  }
  JSObject* wrapper = nsWrapperCache::GetWrapper();
  JSAutoCompartment ac(cx, wrapper);
  JS::Value v;
  if (!mozilla::dom::WrapObject(cx, wrapper, item, item, nullptr, &v)) {
    error.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }
  return &v.toObject();
}

void
HTMLOptionsCollection::GetSupportedNames(nsTArray<nsString>& aNames)
{
  nsAutoTArray<nsIAtom*, 8> atoms;
  for (uint32_t i = 0; i < mElements.Length(); ++i) {
    HTMLOptionElement* content = mElements.ElementAt(i);
    if (content) {
      // Note: HasName means the names is exposed on the document,
      // which is false for options, so we don't check it here.
      const nsAttrValue* val = content->GetParsedAttr(nsGkAtoms::name);
      if (val && val->Type() == nsAttrValue::eAtom) {
        nsIAtom* name = val->GetAtomValue();
        if (!atoms.Contains(name)) {
          atoms.AppendElement(name);
        }
      }
      if (content->HasID()) {
        nsIAtom* id = content->GetID();
        if (!atoms.Contains(id)) {
          atoms.AppendElement(id);
        }
      }
    }
  }

  aNames.SetCapacity(atoms.Length());
  for (uint32_t i = 0; i < atoms.Length(); ++i) {
    aNames.AppendElement(nsDependentAtomString(atoms[i]));
  }
}

NS_IMETHODIMP
HTMLOptionsCollection::GetSelect(nsIDOMHTMLSelectElement** aReturn)
{
  NS_IF_ADDREF(*aReturn = mSelect);
  return NS_OK;
}

NS_IMETHODIMP
HTMLOptionsCollection::Add(nsIDOMHTMLOptionElement* aOption,
                           nsIVariant* aBefore)
{
  if (!aOption) {
    return NS_ERROR_INVALID_ARG;
  }

  if (!mSelect) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  return mSelect->Add(aOption, aBefore);
}

void
HTMLOptionsCollection::Add(const HTMLOptionOrOptGroupElement& aElement,
                           const Nullable<HTMLElementOrLong>& aBefore,
                           ErrorResult& aError)
{
  mSelect->Add(aElement, aBefore, aError);
}

void
HTMLOptionsCollection::Remove(int32_t aIndex, ErrorResult& aError)
{
  if (!mSelect) {
    aError.Throw(NS_ERROR_UNEXPECTED);
    return;
  }

  uint32_t len = 0;
  mSelect->GetLength(&len);
  if (aIndex < 0 || (uint32_t)aIndex >= len)
    aIndex = 0;

  aError = mSelect->Remove(aIndex);
}

NS_IMETHODIMP
HTMLOptionsCollection::Remove(int32_t aIndex)
{
  ErrorResult rv;
  Remove(aIndex, rv);
  return rv.ErrorCode();
}

} // namespace dom
} // namespace mozilla
