// Copyright 2007-2019 David Robillard <d@drobilla.net>
// SPDX-License-Identifier: ISC

#include "lilv_internal.h"

#include <lilv/lilv.h>
#include <serd/serd.h>
#include <sord/sord.h>
#include <zix/allocator.h>
#include <zix/filesystem.h>
#include <zix/path.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
lilv_node_set_numerics_from_string(LilvNode* val)
{
  const char* str = (const char*)sord_node_get_string(val->node);

  switch (val->type) {
  case LILV_VALUE_URI:
  case LILV_VALUE_BLANK:
  case LILV_VALUE_STRING:
  case LILV_VALUE_BLOB:
    break;
  case LILV_VALUE_INT:
    val->val.int_val = strtol(str, NULL, 10);
    break;
  case LILV_VALUE_FLOAT:
    val->val.float_val = serd_strtod(str, NULL);
    break;
  case LILV_VALUE_BOOL:
    val->val.bool_val = !strcmp(str, "true");
    break;
  }
}

/** Note that if `type` is numeric or boolean, the returned value is corrupt
 * until lilv_node_set_numerics_from_string is called.  It is not
 * automatically called from here to avoid overhead and imprecision when the
 * exact string value is known.
 */
LilvNode*
lilv_node_new(LilvWorld* world, LilvNodeType type, const char* str)
{
  LilvNode* val = (LilvNode*)malloc(sizeof(LilvNode));
  val->world    = world;
  val->type     = type;

  const uint8_t* ustr = (const uint8_t*)str;
  switch (type) {
  case LILV_VALUE_URI:
    val->node = sord_new_uri(world->world, ustr);
    break;
  case LILV_VALUE_BLANK:
    val->node = sord_new_blank(world->world, ustr);
    break;
  case LILV_VALUE_STRING:
    val->node = sord_new_literal(world->world, NULL, ustr, NULL);
    break;
  case LILV_VALUE_INT:
    val->node =
      sord_new_literal(world->world, world->uris.xsd_integer, ustr, NULL);
    break;
  case LILV_VALUE_FLOAT:
    val->node =
      sord_new_literal(world->world, world->uris.xsd_decimal, ustr, NULL);
    break;
  case LILV_VALUE_BOOL:
    val->node =
      sord_new_literal(world->world, world->uris.xsd_boolean, ustr, NULL);
    break;
  case LILV_VALUE_BLOB:
    val->node =
      sord_new_literal(world->world, world->uris.xsd_base64Binary, ustr, NULL);
    break;
  }

  if (!val->node) {
    free(val);
    return NULL;
  }

  return val;
}

/** Create a new LilvNode from `node`, or return NULL if impossible */
LilvNode*
lilv_node_new_from_node(LilvWorld* world, const SordNode* node)
{
  if (!node) {
    return NULL;
  }

  LilvNode*       result       = NULL;
  const SordNode* datatype_uri = NULL;
  LilvNodeType    type         = LILV_VALUE_STRING;

  switch (sord_node_get_type(node)) {
  case SORD_URI:
    result        = (LilvNode*)malloc(sizeof(LilvNode));
    result->world = world;
    result->type  = LILV_VALUE_URI;
    result->node  = sord_node_copy(node);
    break;
  case SORD_BLANK:
    result        = (LilvNode*)malloc(sizeof(LilvNode));
    result->world = world;
    result->type  = LILV_VALUE_BLANK;
    result->node  = sord_node_copy(node);
    break;
  case SORD_LITERAL:
    datatype_uri = sord_node_get_datatype(node);
    if (datatype_uri) {
      if (sord_node_equals(datatype_uri, world->uris.xsd_boolean)) {
        type = LILV_VALUE_BOOL;
      } else if (sord_node_equals(datatype_uri, world->uris.xsd_decimal) ||
                 sord_node_equals(datatype_uri, world->uris.xsd_double)) {
        type = LILV_VALUE_FLOAT;
      } else if (sord_node_equals(datatype_uri, world->uris.xsd_integer)) {
        type = LILV_VALUE_INT;
      } else if (sord_node_equals(datatype_uri, world->uris.xsd_base64Binary)) {
        type = LILV_VALUE_BLOB;
      } else {
        LILV_ERRORF("Unknown datatype `%s'\n",
                    sord_node_get_string(datatype_uri));
      }
    }
    result =
      lilv_node_new(world, type, (const char*)sord_node_get_string(node));
    lilv_node_set_numerics_from_string(result);
    break;
  }

  return result;
}

LilvNode*
lilv_new_uri(LilvWorld* world, const char* uri)
{
  return lilv_node_new(world, LILV_VALUE_URI, uri);
}

LilvNode*
lilv_new_file_uri(LilvWorld* world, const char* host, const char* path)
{
  SerdNode s = SERD_NODE_NULL;
  if (zix_path_root_directory(path).length) {
    s = serd_node_new_file_uri(
      (const uint8_t*)path, (const uint8_t*)host, NULL, true);
  } else {
    char* const cwd      = zix_current_path(NULL);
    char* const abs_path = zix_path_join(NULL, cwd, path);

    s = serd_node_new_file_uri(
      (const uint8_t*)abs_path, (const uint8_t*)host, NULL, true);

    zix_free(NULL, abs_path);
    zix_free(NULL, cwd);
  }

  LilvNode* ret = lilv_node_new(world, LILV_VALUE_URI, (const char*)s.buf);
  serd_node_free(&s);
  return ret;
}

LilvNode*
lilv_new_string(LilvWorld* world, const char* str)
{
  return lilv_node_new(world, LILV_VALUE_STRING, str);
}

LilvNode*
lilv_new_int(LilvWorld* world, int val)
{
  char str[32];
  snprintf(str, sizeof(str), "%d", val);
  LilvNode* ret = lilv_node_new(world, LILV_VALUE_INT, str);
  if (ret) {
    ret->val.int_val = val;
  }
  return ret;
}

LilvNode*
lilv_new_float(LilvWorld* world, float val)
{
  char str[32];
  snprintf(str, sizeof(str), "%f", val);
  LilvNode* ret = lilv_node_new(world, LILV_VALUE_FLOAT, str);
  if (ret) {
    ret->val.float_val = val;
  }
  return ret;
}

LilvNode*
lilv_new_bool(LilvWorld* world, bool val)
{
  LilvNode* ret = lilv_node_new(world, LILV_VALUE_BOOL, val ? "true" : "false");
  if (ret) {
    ret->val.bool_val = val;
  }
  return ret;
}

LilvNode*
lilv_node_duplicate(const LilvNode* val)
{
  if (!val) {
    return NULL;
  }

  LilvNode* result = (LilvNode*)malloc(sizeof(LilvNode));
  result->world    = val->world;
  result->node     = sord_node_copy(val->node);
  result->val      = val->val;
  result->type     = val->type;
  return result;
}

void
lilv_node_free(LilvNode* val)
{
  if (val) {
    sord_node_free(val->world->world, val->node);
    free(val);
  }
}

bool
lilv_node_equals(const LilvNode* value, const LilvNode* other)
{
  if (value == NULL && other == NULL) {
    return true;
  }

  if (value == NULL || other == NULL || value->type != other->type) {
    return false;
  }

  switch (value->type) {
  case LILV_VALUE_URI:
  case LILV_VALUE_BLANK:
  case LILV_VALUE_STRING:
  case LILV_VALUE_BLOB:
    return sord_node_equals(value->node, other->node);
  case LILV_VALUE_INT:
    return (value->val.int_val == other->val.int_val);
  case LILV_VALUE_FLOAT:
    return (value->val.float_val == other->val.float_val);
  case LILV_VALUE_BOOL:
    return (value->val.bool_val == other->val.bool_val);
  }

  return false; /* shouldn't get here */
}

char*
lilv_node_get_turtle_token(const LilvNode* value)
{
  const char* str    = (const char*)sord_node_get_string(value->node);
  size_t      len    = 0;
  char*       result = NULL;
  SerdNode    node;

  switch (value->type) {
  case LILV_VALUE_URI:
    len    = strlen(str) + 3;
    result = (char*)calloc(len, 1);
    snprintf(result, len, "<%s>", str);
    break;
  case LILV_VALUE_BLANK:
    len    = strlen(str) + 3;
    result = (char*)calloc(len, 1);
    snprintf(result, len, "_:%s", str);
    break;
  case LILV_VALUE_STRING:
  case LILV_VALUE_BOOL:
  case LILV_VALUE_BLOB:
    result = lilv_strdup(str);
    break;
  case LILV_VALUE_INT:
    node   = serd_node_new_integer(value->val.int_val);
    result = lilv_strdup((char*)node.buf);
    serd_node_free(&node);
    break;
  case LILV_VALUE_FLOAT:
    node   = serd_node_new_decimal(value->val.float_val, 8);
    result = lilv_strdup((char*)node.buf);
    serd_node_free(&node);
    break;
  }

  return result;
}

bool
lilv_node_is_uri(const LilvNode* value)
{
  return (value && value->type == LILV_VALUE_URI);
}

const char*
lilv_node_as_uri(const LilvNode* value)
{
  return (lilv_node_is_uri(value)
            ? (const char*)sord_node_get_string(value->node)
            : NULL);
}

bool
lilv_node_is_blank(const LilvNode* value)
{
  return (value && value->type == LILV_VALUE_BLANK);
}

const char*
lilv_node_as_blank(const LilvNode* value)
{
  return (lilv_node_is_blank(value)
            ? (const char*)sord_node_get_string(value->node)
            : NULL);
}

bool
lilv_node_is_literal(const LilvNode* value)
{
  if (!value) {
    return false;
  }

  switch (value->type) {
  case LILV_VALUE_STRING:
  case LILV_VALUE_INT:
  case LILV_VALUE_FLOAT:
  case LILV_VALUE_BLOB:
    return true;
  default:
    return false;
  }
}

bool
lilv_node_is_string(const LilvNode* value)
{
  return (value && value->type == LILV_VALUE_STRING);
}

const char*
lilv_node_as_string(const LilvNode* value)
{
  return value ? (const char*)sord_node_get_string(value->node) : NULL;
}

bool
lilv_node_is_int(const LilvNode* value)
{
  return (value && value->type == LILV_VALUE_INT);
}

int
lilv_node_as_int(const LilvNode* value)
{
  return lilv_node_is_int(value) ? value->val.int_val : 0;
}

bool
lilv_node_is_float(const LilvNode* value)
{
  return (value && value->type == LILV_VALUE_FLOAT);
}

float
lilv_node_as_float(const LilvNode* value)
{
  if (lilv_node_is_float(value)) {
    return value->val.float_val;
  }

  if (lilv_node_is_int(value)) {
    return (float)value->val.int_val;
  }

  return NAN;
}

bool
lilv_node_is_bool(const LilvNode* value)
{
  return (value && value->type == LILV_VALUE_BOOL);
}

bool
lilv_node_as_bool(const LilvNode* value)
{
  return lilv_node_is_bool(value) ? value->val.bool_val : false;
}

char*
lilv_node_get_path(const LilvNode* value, char** hostname)
{
  if (lilv_node_is_uri(value)) {
    return lilv_file_uri_parse(lilv_node_as_uri(value), hostname);
  }
  return NULL;
}
