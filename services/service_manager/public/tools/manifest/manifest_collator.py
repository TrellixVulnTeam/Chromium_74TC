#!/usr/bin/env python
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

""" A collator for Service Manifests """

import argparse
import json
import os
import shutil
import sys
import urlparse


# Keys which are completely overridden by manifest overlays
_MANIFEST_OVERLAY_OVERRIDE_KEYS = [
  "display_name",
  "process-group",
]

eater_relative = '../../../../../../tools/json_comment_eater'
eater_relative = os.path.join(os.path.abspath(__file__), eater_relative)
sys.path.insert(0, os.path.normpath(eater_relative))
try:
  import json_comment_eater
finally:
  sys.path.pop(0)


def ParseJSONFile(filename):
  with open(filename) as json_file:
    try:
      return json.loads(json_comment_eater.Nom(json_file.read()))
    except ValueError as e:
      print "%s is not a valid JSON document" % filename
      raise e


def MergeDicts(left, right):
  for k, v in right.iteritems():
    if k not in left:
      left[k] = v
    else:
      if isinstance(v, dict):
        assert isinstance(left[k], dict)
        MergeDicts(left[k], v)
      elif isinstance(v, list):
        assert isinstance(left[k], list)
        left[k].extend(v)
      else:
        raise "Refusing to merge conflicting non-collection values."
  return left


def MergeManifestOverlay(manifest, overlay):
  MergeDicts(manifest["capabilities"], overlay["capabilities"])

  if "services" in overlay:
    if "services" not in manifest:
      manifest["services"] = []
    manifest["services"].extend(overlay["services"])

  for key in _MANIFEST_OVERLAY_OVERRIDE_KEYS:
    if key in overlay:
      manifest[key] = overlay[key]


def main():
  parser = argparse.ArgumentParser(
      description="Collate Service Manifests.")
  parser.add_argument("--parent")
  parser.add_argument("--output")
  parser.add_argument("--name")
  parser.add_argument("--overlays", nargs="+", dest="overlays", default=[])
  args, children = parser.parse_known_args()

  parent = ParseJSONFile(args.parent)

  service_path = parent['name'].split(':')[1]
  if service_path.startswith('//'):
    raise ValueError("Service name path component '%s' must not start " \
                     "with //" % service_path)

  if args.name != service_path:
    raise ValueError("Service name '%s' specified in build file does not " \
                     "match name '%s' specified in manifest." %
                     (args.name, service_path))

  services = []
  for child in children:
    services.append(ParseJSONFile(child))

  if len(services) > 0:
    parent['services'] = services

  for overlay_path in args.overlays:
    MergeManifestOverlay(parent, ParseJSONFile(overlay_path))

  with open(args.output, 'w') as output_file:
    json.dump(parent, output_file)

  return 0

if __name__ == "__main__":
  sys.exit(main())
