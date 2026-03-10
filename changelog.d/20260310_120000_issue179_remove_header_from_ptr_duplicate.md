---
bump: patch
---

### Changed
- Removed the non-templated `header_from_ptr()` overload hardcoded for `DefaultAddressTraits` from `types.h` (Issue #179). The templated `header_from_ptr_t<AddressTraitsT>()` already covers all address-trait configurations generically, eliminating the duplicate code.
