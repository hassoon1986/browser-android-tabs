// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_ACCESSIBILITY_BROWSER_ACCESSIBILITY_COM_WIN_H_
#define CONTENT_BROWSER_ACCESSIBILITY_BROWSER_ACCESSIBILITY_COM_WIN_H_

#include <oleacc.h>
#include <stddef.h>
#include <stdint.h>
#include <vector>

#include "base/compiler_specific.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/win/atl.h"
#include "content/browser/accessibility/browser_accessibility.h"
#include "content/browser/accessibility/browser_accessibility_win.h"
#include "content/common/content_export.h"
#include "third_party/iaccessible2/ia2_api_all.h"
#include "third_party/isimpledom/ISimpleDOMDocument.h"
#include "third_party/isimpledom/ISimpleDOMNode.h"
#include "third_party/isimpledom/ISimpleDOMText.h"
#include "ui/accessibility/platform/ax_platform_node_delegate.h"
#include "ui/accessibility/platform/ax_platform_node_win.h"

// These nonstandard GUIDs are taken directly from the Mozilla sources
// (accessible/src/msaa/nsAccessNodeWrap.cpp); some documentation is here:
// http://developer.mozilla.org/en/Accessibility/AT-APIs/ImplementationFeatures/MSAA
const GUID GUID_ISimpleDOM = {0x0c539790,
                              0x12e4,
                              0x11cf,
                              {0xb6, 0x61, 0x00, 0xaa, 0x00, 0x4c, 0xd6, 0xd8}};
const GUID GUID_IAccessibleContentDocument = {
    0xa5d8e1f3,
    0x3571,
    0x4d8f,
    {0x95, 0x21, 0x07, 0xed, 0x28, 0xfb, 0x07, 0x2e}};

namespace ui {

enum class AXTextBoundaryDirection;

}  // namespace ui

namespace content {
class BrowserAccessibilityWin;

////////////////////////////////////////////////////////////////////////////////
//
// BrowserAccessibilityComWin
//
// Class implementing the windows accessible interface used by screen readers
// and other assistive technology (AT). It typically is created and owned by
// a BrowserAccessibilityWin |owner_|. When this owner goes away, the
// BrowserAccessibilityComWin objects may continue to exists being held onto by
// MSCOM (due to reference counting). However, such objects are invalid and
// should gracefully fail by returning E_FAIL from all MSCOM methods.
//
////////////////////////////////////////////////////////////////////////////////
class __declspec(uuid("562072fe-3390-43b1-9e2c-dd4118f5ac79"))
    BrowserAccessibilityComWin : public ui::AXPlatformNodeWin,
                                 public IAccessibleApplication,
                                 public IAccessibleHyperlink,
                                 public IAccessibleHypertext,
                                 public IAccessibleImage,
                                 public IAccessibleValue,
                                 public ISimpleDOMDocument,
                                 public ISimpleDOMNode,
                                 public ISimpleDOMText {
 public:
  BEGIN_COM_MAP(BrowserAccessibilityComWin)
  COM_INTERFACE_ENTRY(IAccessibleAction)
  COM_INTERFACE_ENTRY(IAccessibleApplication)
  COM_INTERFACE_ENTRY(IAccessibleHyperlink)
  COM_INTERFACE_ENTRY(IAccessibleHypertext)
  COM_INTERFACE_ENTRY(IAccessibleImage)
  COM_INTERFACE_ENTRY(IAccessibleValue)
  COM_INTERFACE_ENTRY(ISimpleDOMDocument)
  COM_INTERFACE_ENTRY(ISimpleDOMNode)
  COM_INTERFACE_ENTRY(ISimpleDOMText)
  COM_INTERFACE_ENTRY_CHAIN(ui::AXPlatformNodeWin)
  END_COM_MAP()

  // Mappings from roles and states to human readable strings. Initialize
  // with |InitializeStringMaps|.
  static std::map<int32_t, base::string16> role_string_map;
  static std::map<int32_t, base::string16> state_string_map;

  CONTENT_EXPORT BrowserAccessibilityComWin();
  CONTENT_EXPORT ~BrowserAccessibilityComWin() override;

  // Called after an atomic tree update completes. See
  // BrowserAccessibilityManagerWin::OnAtomicUpdateFinished for more
  // details on what these do.
  CONTENT_EXPORT void UpdateStep1ComputeWinAttributes();
  CONTENT_EXPORT void UpdateStep2ComputeHypertext();
  CONTENT_EXPORT void UpdateStep3FireEvents(bool is_subtree_creation);

  //
  // IAccessible2 methods.
  //
  CONTENT_EXPORT IFACEMETHODIMP get_attributes(BSTR* attributes) override;

  CONTENT_EXPORT IFACEMETHODIMP
  scrollTo(enum IA2ScrollType scroll_type) override;

  //
  // IAccessibleApplication methods.
  //
  CONTENT_EXPORT IFACEMETHODIMP get_appName(BSTR* app_name) override;

  CONTENT_EXPORT IFACEMETHODIMP get_appVersion(BSTR* app_version) override;

  CONTENT_EXPORT IFACEMETHODIMP get_toolkitName(BSTR* toolkit_name) override;

  CONTENT_EXPORT IFACEMETHODIMP
  get_toolkitVersion(BSTR* toolkit_version) override;

  //
  // IAccessibleImage methods.
  //
  CONTENT_EXPORT IFACEMETHODIMP get_description(BSTR* description) override;

  CONTENT_EXPORT IFACEMETHODIMP
  get_imagePosition(enum IA2CoordinateType coordinate_type,
                    LONG* x,
                    LONG* y) override;

  CONTENT_EXPORT IFACEMETHODIMP get_imageSize(LONG* height,
                                              LONG* width) override;

  //
  // IAccessibleText methods.
  //

  CONTENT_EXPORT IFACEMETHODIMP get_nCharacters(LONG* n_characters) override;

  CONTENT_EXPORT IFACEMETHODIMP get_caretOffset(LONG* offset) override;

  CONTENT_EXPORT IFACEMETHODIMP
  get_characterExtents(LONG offset,
                       enum IA2CoordinateType coord_type,
                       LONG* out_x,
                       LONG* out_y,
                       LONG* out_width,
                       LONG* out_height) override;

  CONTENT_EXPORT IFACEMETHODIMP get_nSelections(LONG* n_selections) override;

  CONTENT_EXPORT IFACEMETHODIMP get_selection(LONG selection_index,
                                              LONG* start_offset,
                                              LONG* end_offset) override;

  CONTENT_EXPORT IFACEMETHODIMP get_text(LONG start_offset,
                                         LONG end_offset,
                                         BSTR* text) override;

  CONTENT_EXPORT IFACEMETHODIMP
  get_textAtOffset(LONG offset,
                   enum IA2TextBoundaryType boundary_type,
                   LONG* start_offset,
                   LONG* end_offset,
                   BSTR* text) override;

  CONTENT_EXPORT IFACEMETHODIMP
  get_textBeforeOffset(LONG offset,
                       enum IA2TextBoundaryType boundary_type,
                       LONG* start_offset,
                       LONG* end_offset,
                       BSTR* text) override;

  CONTENT_EXPORT IFACEMETHODIMP
  get_textAfterOffset(LONG offset,
                      enum IA2TextBoundaryType boundary_type,
                      LONG* start_offset,
                      LONG* end_offset,
                      BSTR* text) override;

  CONTENT_EXPORT IFACEMETHODIMP get_newText(IA2TextSegment* new_text) override;

  CONTENT_EXPORT IFACEMETHODIMP get_oldText(IA2TextSegment* old_text) override;

  CONTENT_EXPORT IFACEMETHODIMP
  get_offsetAtPoint(LONG x,
                    LONG y,
                    enum IA2CoordinateType coord_type,
                    LONG* offset) override;

  CONTENT_EXPORT IFACEMETHODIMP
  scrollSubstringTo(LONG start_index,
                    LONG end_index,
                    enum IA2ScrollType scroll_type) override;

  CONTENT_EXPORT IFACEMETHODIMP
  scrollSubstringToPoint(LONG start_index,
                         LONG end_index,
                         enum IA2CoordinateType coordinate_type,
                         LONG x,
                         LONG y) override;

  CONTENT_EXPORT IFACEMETHODIMP addSelection(LONG start_offset,
                                             LONG end_offset) override;

  CONTENT_EXPORT IFACEMETHODIMP removeSelection(LONG selection_index) override;

  CONTENT_EXPORT IFACEMETHODIMP setCaretOffset(LONG offset) override;

  CONTENT_EXPORT IFACEMETHODIMP setSelection(LONG selection_index,
                                             LONG start_offset,
                                             LONG end_offset) override;

  // IAccessibleText methods not implemented.
  CONTENT_EXPORT IFACEMETHODIMP get_attributes(LONG offset,
                                               LONG* start_offset,
                                               LONG* end_offset,
                                               BSTR* text_attributes) override;

  //
  // IAccessibleHypertext methods.
  //

  CONTENT_EXPORT IFACEMETHODIMP get_nHyperlinks(LONG* hyperlink_count) override;

  CONTENT_EXPORT IFACEMETHODIMP
  get_hyperlink(LONG index, IAccessibleHyperlink** hyperlink) override;

  CONTENT_EXPORT IFACEMETHODIMP
  get_hyperlinkIndex(LONG char_index, LONG* hyperlink_index) override;

  // IAccessibleHyperlink methods.
  CONTENT_EXPORT IFACEMETHODIMP get_anchor(LONG index,
                                           VARIANT* anchor) override;
  CONTENT_EXPORT IFACEMETHODIMP
  get_anchorTarget(LONG index, VARIANT* anchor_target) override;
  CONTENT_EXPORT IFACEMETHODIMP get_startIndex(LONG* index) override;
  CONTENT_EXPORT IFACEMETHODIMP get_endIndex(LONG* index) override;
  // This method is deprecated in the IA2 Spec and so we don't implement it.
  CONTENT_EXPORT IFACEMETHODIMP get_valid(boolean* valid) override;

  // IAccessibleAction mostly not implemented.
  CONTENT_EXPORT IFACEMETHODIMP nActions(LONG* n_actions) override;
  CONTENT_EXPORT IFACEMETHODIMP doAction(LONG action_index) override;
  CONTENT_EXPORT IFACEMETHODIMP get_description(LONG action_index,
                                                BSTR* description) override;
  CONTENT_EXPORT IFACEMETHODIMP get_keyBinding(LONG action_index,
                                               LONG n_max_bindings,
                                               BSTR** key_bindings,
                                               LONG* n_bindings) override;
  CONTENT_EXPORT IFACEMETHODIMP get_name(LONG action_index,
                                         BSTR* name) override;
  CONTENT_EXPORT IFACEMETHODIMP
  get_localizedName(LONG action_index, BSTR* localized_name) override;

  //
  // IAccessibleValue methods.
  //

  CONTENT_EXPORT IFACEMETHODIMP get_currentValue(VARIANT* value) override;

  CONTENT_EXPORT IFACEMETHODIMP get_minimumValue(VARIANT* value) override;

  CONTENT_EXPORT IFACEMETHODIMP get_maximumValue(VARIANT* value) override;

  CONTENT_EXPORT IFACEMETHODIMP setCurrentValue(VARIANT new_value) override;

  //
  // ISimpleDOMDocument methods.
  //

  CONTENT_EXPORT IFACEMETHODIMP get_URL(BSTR* url) override;

  CONTENT_EXPORT IFACEMETHODIMP get_title(BSTR* title) override;

  CONTENT_EXPORT IFACEMETHODIMP get_mimeType(BSTR* mime_type) override;

  CONTENT_EXPORT IFACEMETHODIMP get_docType(BSTR* doc_type) override;

  CONTENT_EXPORT IFACEMETHODIMP
  get_nameSpaceURIForID(SHORT name_space_id, BSTR* name_space_uri) override;
  CONTENT_EXPORT IFACEMETHODIMP
  put_alternateViewMediaTypes(BSTR* comma_separated_media_types) override;

  //
  // ISimpleDOMNode methods.
  //

  CONTENT_EXPORT IFACEMETHODIMP get_nodeInfo(BSTR* node_name,
                                             SHORT* name_space_id,
                                             BSTR* node_value,
                                             unsigned int* num_children,
                                             unsigned int* unique_id,
                                             USHORT* node_type) override;

  CONTENT_EXPORT IFACEMETHODIMP get_attributes(USHORT max_attribs,
                                               BSTR* attrib_names,
                                               SHORT* name_space_id,
                                               BSTR* attrib_values,
                                               USHORT* num_attribs) override;

  CONTENT_EXPORT IFACEMETHODIMP
  get_attributesForNames(USHORT num_attribs,
                         BSTR* attrib_names,
                         SHORT* name_space_id,
                         BSTR* attrib_values) override;

  CONTENT_EXPORT IFACEMETHODIMP
  get_computedStyle(USHORT max_style_properties,
                    boolean use_alternate_view,
                    BSTR* style_properties,
                    BSTR* style_values,
                    USHORT* num_style_properties) override;

  CONTENT_EXPORT IFACEMETHODIMP
  get_computedStyleForProperties(USHORT num_style_properties,
                                 boolean use_alternate_view,
                                 BSTR* style_properties,
                                 BSTR* style_values) override;

  CONTENT_EXPORT IFACEMETHODIMP scrollTo(boolean placeTopLeft) override;

  CONTENT_EXPORT IFACEMETHODIMP get_parentNode(ISimpleDOMNode** node) override;

  CONTENT_EXPORT IFACEMETHODIMP get_firstChild(ISimpleDOMNode** node) override;

  CONTENT_EXPORT IFACEMETHODIMP get_lastChild(ISimpleDOMNode** node) override;

  CONTENT_EXPORT IFACEMETHODIMP
  get_previousSibling(ISimpleDOMNode** node) override;

  CONTENT_EXPORT IFACEMETHODIMP get_nextSibling(ISimpleDOMNode** node) override;

  CONTENT_EXPORT IFACEMETHODIMP get_childAt(unsigned int child_index,
                                            ISimpleDOMNode** node) override;

  CONTENT_EXPORT IFACEMETHODIMP get_innerHTML(BSTR* innerHTML) override;

  CONTENT_EXPORT IFACEMETHODIMP
  get_localInterface(void** local_interface) override;

  CONTENT_EXPORT IFACEMETHODIMP get_language(BSTR* language) override;

  //
  // ISimpleDOMText methods.
  //

  CONTENT_EXPORT IFACEMETHODIMP get_domText(BSTR* dom_text) override;

  CONTENT_EXPORT IFACEMETHODIMP
  get_clippedSubstringBounds(unsigned int start_index,
                             unsigned int end_index,
                             int* out_x,
                             int* out_y,
                             int* out_width,
                             int* out_height) override;

  CONTENT_EXPORT IFACEMETHODIMP
  get_unclippedSubstringBounds(unsigned int start_index,
                               unsigned int end_index,
                               int* out_x,
                               int* out_y,
                               int* out_width,
                               int* out_height) override;

  CONTENT_EXPORT IFACEMETHODIMP
  scrollToSubstring(unsigned int start_index, unsigned int end_index) override;

  CONTENT_EXPORT IFACEMETHODIMP get_fontFamily(BSTR* font_family) override;

  //
  // IServiceProvider methods.
  //

  CONTENT_EXPORT IFACEMETHODIMP QueryService(REFGUID guidService,
                                             REFIID riid,
                                             void** object) override;

  //
  // CComObjectRootEx methods.
  //

  // Called by BEGIN_COM_MAP() / END_COM_MAP().
  static CONTENT_EXPORT STDMETHODIMP
  InternalQueryInterface(void* this_ptr,
                         const _ATL_INTMAP_ENTRY* entries,
                         REFIID iid,
                         void** object);

  // Computes and caches the IA2 text style attributes for the text and other
  // embedded child objects.
  CONTENT_EXPORT void ComputeStylesIfNeeded();

  // Public accessors (these do not have COM accessible accessors)
  const base::string16& role_name() const { return win_attributes_->role_name; }
  const std::map<int, std::vector<base::string16>>& offset_to_text_attributes()
      const {
    return win_attributes_->offset_to_text_attributes;
  }

 private:
  // Private accessors.
  const std::vector<base::string16>& ia2_attributes() const {
    return win_attributes_->ia2_attributes;
  }
  base::string16 name() const { return win_attributes_->name; }
  base::string16 description() const { return win_attributes_->description; }
  base::string16 value() const { return win_attributes_->value; }

  // Setter and getter for the browser accessibility owner
  BrowserAccessibilityWin* owner() const { return owner_; }
  void SetOwner(BrowserAccessibilityWin* owner) { owner_ = owner; }

  BrowserAccessibilityManager* Manager() const;

  //
  // AXPlatformNode overrides
  //
  void Destroy() override;
  void Init(ui::AXPlatformNodeDelegate* delegate) override;

  // Returns the IA2 text attributes for this object.
  std::vector<base::string16> ComputeTextAttributes() const;

  // Add one to the reference count and return the same object. Always
  // use this method when returning a BrowserAccessibilityComWin object as
  // an output parameter to a COM interface, never use it otherwise.
  BrowserAccessibilityComWin* NewReference();

  // Returns a list of IA2 attributes indicating the offsets in the text of a
  // leaf object, such as a text field or static text, where spelling errors are
  // present.
  std::map<int, std::vector<base::string16>> GetSpellingAttributes();

  // Many MSAA methods take a var_id parameter indicating that the operation
  // should be performed on a particular child ID, rather than this object.
  // This method tries to figure out the target object from |var_id| and
  // returns a pointer to the target object if it exists, otherwise NULL.
  // Does not return a new reference.
  BrowserAccessibilityComWin* GetTargetFromChildID(const VARIANT& var_id);

  // Retrieve the value of an attribute from the string attribute map and
  // if found and nonempty, allocate a new BSTR (with SysAllocString)
  // and return S_OK. If not found or empty, return S_FALSE.
  HRESULT GetStringAttributeAsBstr(ax::mojom::StringAttribute attribute,
                                   BSTR* value_bstr);

  // Merges the given spelling attributes, i.e. document marker information,
  // into the given text attributes starting at the given character offset. This
  // is required for two reasons: 1. Document markers that are present on text
  // leaves need to be propagated to their parent object for compatibility with
  // Firefox. 2. Spelling markers need to overwrite any aria-invalid="false" in
  // the text attributes.
  static void MergeSpellingIntoTextAttributes(
      const std::map<int, std::vector<base::string16>>& spelling_attributes,
      int start_offset,
      std::map<int, std::vector<base::string16>>* text_attributes);

  // Escapes characters in string attributes as required by the IA2 Spec.
  // It's okay for input to be the same as output.
  CONTENT_EXPORT static void SanitizeStringAttributeForIA2(
      const base::string16& input,
      base::string16* output);
  FRIEND_TEST_ALL_PREFIXES(BrowserAccessibilityTest,
                           TestSanitizeStringAttributeForIA2);
  FRIEND_TEST_ALL_PREFIXES(BrowserAccessibilityWinTest,
                           TestSanitizeStringAttributeForIA2);

  // Sets the selection given a start and end offset in IA2 Hypertext.
  void SetIA2HypertextSelection(LONG start_offset, LONG end_offset);

  // Search forwards or backwards from the given offset until the given IA2
  // text boundary is found, and return the offset of that boundary.
  LONG FindIA2Boundary(IA2TextBoundaryType ia2_boundary,
                       LONG start_offset,
                       ui::AXTextBoundaryDirection direction);

  // Searches forward from the given offset until the start of the next style
  // is found, or searches backward from the given offset until the start of the
  // current style is found.
  LONG FindStartOfStyle(LONG start_offset,
                        ui::AXTextBoundaryDirection direction);

  // ID refers to the node ID in the current tree, not the globally unique ID.
  // TODO(nektar): Could we use globally unique IDs everywhere?
  // TODO(nektar): Rename this function to GetFromNodeID.
  BrowserAccessibilityComWin* GetFromID(int32_t id) const;

  // Returns true if this is a list box option with a parent of type list box,
  // or a menu list option with a parent of type menu list popup.
  bool IsListBoxOptionOrMenuListOption();

  // Fire a Windows-specific accessibility event notification on this node.
  void FireNativeEvent(LONG win_event_type) const;
  struct WinAttributes {
    WinAttributes();
    ~WinAttributes();

    // Ignored state
    bool ignored;

    // IAccessible role and state.
    int32_t ia_role;
    int32_t ia_state;
    base::string16 role_name;

    // IAccessible name, description, help, value.
    base::string16 name;
    base::string16 description;
    base::string16 value;

    // IAccessible2 role and state.
    int32_t ia2_role;
    int32_t ia2_state;

    // IAccessible2 attributes.
    std::vector<base::string16> ia2_attributes;

    // Maps each style span to its start offset in hypertext.
    std::map<int, std::vector<base::string16>> offset_to_text_attributes;
  };

  BrowserAccessibilityWin* owner_;

  std::unique_ptr<WinAttributes> win_attributes_;

  // Only valid during the scope of a IA2_EVENT_TEXT_REMOVED or
  // IA2_EVENT_TEXT_INSERTED event.
  std::unique_ptr<WinAttributes> old_win_attributes_;

  // The previous scroll position, so we can tell if this object scrolled.
  int previous_scroll_x_;
  int previous_scroll_y_;

  // Give BrowserAccessibility::Create access to our constructor.
  friend class BrowserAccessibility;
  friend class BrowserAccessibilityWin;

  DISALLOW_COPY_AND_ASSIGN(BrowserAccessibilityComWin);
};

CONTENT_EXPORT BrowserAccessibilityComWin* ToBrowserAccessibilityComWin(
    BrowserAccessibility* obj);

}  // namespace content

#endif  // CONTENT_BROWSER_ACCESSIBILITY_BROWSER_ACCESSIBILITY_COM_WIN_H_
