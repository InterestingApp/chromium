// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/extensions/manifest_handlers/externally_connectable.h"

#include <algorithm>

#include "base/stl_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/common/extensions/api/manifest_types.h"
#include "chrome/common/extensions/permissions/permissions_data.h"
#include "extensions/common/error_utils.h"
#include "extensions/common/manifest_constants.h"
#include "extensions/common/permissions/api_permission_set.h"
#include "extensions/common/url_pattern.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/gurl.h"

namespace rcd = net::registry_controlled_domains;

namespace extensions {

namespace externally_connectable_errors {
const char kErrorInvalidMatchPattern[] = "Invalid match pattern '*'";
const char kErrorInvalidId[] = "Invalid ID '*'";
const char kErrorNothingSpecified[] =
    "'externally_connectable' specifies neither 'matches' nor 'ids'; "
    "nothing will be able to connect";
const char kErrorTopLevelDomainsNotAllowed[] =
    "\"*\" is an effective top level domain for which wildcard subdomains such "
    "as \"*\" are not allowed";
const char kErrorWildcardHostsNotAllowed[] =
    "Wildcard domain patterns such as \"*\" are not allowed";
}  // namespace externally_connectable_errors

namespace keys = extensions::manifest_keys;
namespace errors = externally_connectable_errors;
using api::manifest_types::ExternallyConnectable;

namespace {

const char kAllIds[] = "*";

template <typename T>
std::vector<T> Sorted(const std::vector<T>& in) {
  std::vector<T> out = in;
  std::sort(out.begin(), out.end());
  return out;
}

} // namespace

ExternallyConnectableHandler::ExternallyConnectableHandler() {}

ExternallyConnectableHandler::~ExternallyConnectableHandler() {}

bool ExternallyConnectableHandler::Parse(Extension* extension,
                                         string16* error) {
  const base::Value* externally_connectable = NULL;
  CHECK(extension->manifest()->Get(keys::kExternallyConnectable,
                                   &externally_connectable));
  std::vector<InstallWarning> install_warnings;
  scoped_ptr<ExternallyConnectableInfo> info =
      ExternallyConnectableInfo::FromValue(*externally_connectable,
                                           &install_warnings,
                                           error);
  if (!info)
    return false;
  if (!info->matches.is_empty()) {
    PermissionsData::GetInitialAPIPermissions(extension)->insert(
        APIPermission::kWebConnectable);
  }
  extension->AddInstallWarnings(install_warnings);
  extension->SetManifestData(keys::kExternallyConnectable, info.release());
  return true;
}

const std::vector<std::string> ExternallyConnectableHandler::Keys() const {
  return SingleKey(keys::kExternallyConnectable);
}

// static
ExternallyConnectableInfo* ExternallyConnectableInfo::Get(
    const Extension* extension) {
  return static_cast<ExternallyConnectableInfo*>(
      extension->GetManifestData(keys::kExternallyConnectable));
}

// static
scoped_ptr<ExternallyConnectableInfo> ExternallyConnectableInfo::FromValue(
    const base::Value& value,
    std::vector<InstallWarning>* install_warnings,
    string16* error) {
  scoped_ptr<ExternallyConnectable> externally_connectable =
      ExternallyConnectable::FromValue(value, error);
  if (!externally_connectable)
    return scoped_ptr<ExternallyConnectableInfo>();

  URLPatternSet matches;

  if (externally_connectable->matches) {
    for (std::vector<std::string>::iterator it =
             externally_connectable->matches->begin();
         it != externally_connectable->matches->end(); ++it) {
      // Safe to use SCHEME_ALL here; externally_connectable gives a page ->
      // extension communication path, not the other way.
      URLPattern pattern(URLPattern::SCHEME_ALL);
      if (pattern.Parse(*it) != URLPattern::PARSE_SUCCESS) {
        *error = ErrorUtils::FormatErrorMessageUTF16(
            errors::kErrorInvalidMatchPattern, *it);
        return scoped_ptr<ExternallyConnectableInfo>();
      }

      // Wildcard hosts are not allowed.
      if (pattern.host().empty()) {
        // Warning not error for forwards compatibility.
        install_warnings->push_back(InstallWarning(
            ErrorUtils::FormatErrorMessage(
                errors::kErrorWildcardHostsNotAllowed, *it),
            keys::kExternallyConnectable,
            *it));
        continue;
      }

      // Wildcards on subdomains of a TLD are not allowed.
      size_t registry_length = rcd::GetRegistryLength(
          pattern.host(),
          // This means that things that look like TLDs - the foobar in
          // http://google.foobar - count as TLDs.
          rcd::INCLUDE_UNKNOWN_REGISTRIES,
          // This means that effective TLDs like appspot.com count as TLDs;
          // codereview.appspot.com and evil.appspot.com are different.
          rcd::INCLUDE_PRIVATE_REGISTRIES);

      if (registry_length == std::string::npos) {
        // The URL parsing combined with host().empty() should have caught this.
        NOTREACHED() << *it;
        *error = ErrorUtils::FormatErrorMessageUTF16(
            errors::kErrorInvalidMatchPattern, *it);
        return scoped_ptr<ExternallyConnectableInfo>();
      }

      // Broad match patterns like "*.com", "*.co.uk", and even "*.appspot.com"
      // are not allowed. However just "appspot.com" is ok.
      if (registry_length == 0 && pattern.match_subdomains()) {
        // Warning not error for forwards compatibility.
        install_warnings->push_back(InstallWarning(
            ErrorUtils::FormatErrorMessage(
                errors::kErrorTopLevelDomainsNotAllowed,
                pattern.host().c_str(),
                *it),
            keys::kExternallyConnectable,
            *it));
        continue;
      }

      matches.AddPattern(pattern);
    }
  }

  std::vector<std::string> ids;
  bool all_ids = false;

  if (externally_connectable->ids) {
    for (std::vector<std::string>::iterator it =
             externally_connectable->ids->begin();
         it != externally_connectable->ids->end(); ++it) {
      if (*it == kAllIds) {
        all_ids = true;
      } else if (Extension::IdIsValid(*it)) {
        ids.push_back(*it);
      } else {
        *error = ErrorUtils::FormatErrorMessageUTF16(
            errors::kErrorInvalidId, *it);
        return scoped_ptr<ExternallyConnectableInfo>();
      }
    }
  }

  if (!externally_connectable->matches &&
      !externally_connectable->ids) {
    install_warnings->push_back(InstallWarning(
        errors::kErrorNothingSpecified,
        keys::kExternallyConnectable));
  }

  return make_scoped_ptr(
      new ExternallyConnectableInfo(matches, ids, all_ids));
}

ExternallyConnectableInfo::~ExternallyConnectableInfo() {}

ExternallyConnectableInfo::ExternallyConnectableInfo(
    const URLPatternSet& matches,
    const std::vector<std::string>& ids,
    bool all_ids)
    : matches(matches), ids(Sorted(ids)), all_ids(all_ids) {}

bool ExternallyConnectableInfo::IdCanConnect(const std::string& id) {
  if (all_ids)
    return true;
  DCHECK(base::STLIsSorted(ids));
  return std::binary_search(ids.begin(), ids.end(), id);
}

}   // namespace extensions
