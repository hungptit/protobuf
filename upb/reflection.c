
#include "upb/reflection.h"

#include <string.h>
#include "upb/table.int.h"
#include "upb/msg.h"

#include "upb/port_def.inc"

static char field_size[] = {
  0,/* 0 */
  8, /* UPB_DESCRIPTOR_TYPE_DOUBLE */
  4, /* UPB_DESCRIPTOR_TYPE_FLOAT */
  8, /* UPB_DESCRIPTOR_TYPE_INT64 */
  8, /* UPB_DESCRIPTOR_TYPE_UINT64 */
  4, /* UPB_DESCRIPTOR_TYPE_INT32 */
  8, /* UPB_DESCRIPTOR_TYPE_FIXED64 */
  4, /* UPB_DESCRIPTOR_TYPE_FIXED32 */
  1, /* UPB_DESCRIPTOR_TYPE_BOOL */
  sizeof(upb_strview), /* UPB_DESCRIPTOR_TYPE_STRING */
  sizeof(void*), /* UPB_DESCRIPTOR_TYPE_GROUP */
  sizeof(void*), /* UPB_DESCRIPTOR_TYPE_MESSAGE */
  sizeof(upb_strview), /* UPB_DESCRIPTOR_TYPE_BYTES */
  4, /* UPB_DESCRIPTOR_TYPE_UINT32 */
  4, /* UPB_DESCRIPTOR_TYPE_ENUM */
  4, /* UPB_DESCRIPTOR_TYPE_SFIXED32 */
  8, /* UPB_DESCRIPTOR_TYPE_SFIXED64 */
  4, /* UPB_DESCRIPTOR_TYPE_SINT32 */
  8, /* UPB_DESCRIPTOR_TYPE_SINT64 */
};

/* Strings/bytes are special-cased in maps. */
static char _upb_fieldtype_to_mapsize[12] = {
  0,
  1,  /* UPB_TYPE_BOOL */
  4,  /* UPB_TYPE_FLOAT */
  4,  /* UPB_TYPE_INT32 */
  4,  /* UPB_TYPE_UINT32 */
  4,  /* UPB_TYPE_ENUM */
  sizeof(void*),  /* UPB_TYPE_MESSAGE */
  8,  /* UPB_TYPE_DOUBLE */
  8,  /* UPB_TYPE_INT64 */
  8,  /* UPB_TYPE_UINT64 */
  0,  /* UPB_TYPE_STRING */
  0,  /* UPB_TYPE_BYTES */
};

/** upb_msg *******************************************************************/

/* If we always read/write as a consistent type to each address, this shouldn't
 * violate aliasing.
 */
#define PTR_AT(msg, ofs, type) (type*)((char*)msg + ofs)

upb_msg *upb_msg_new(const upb_msgdef *m, upb_arena *a) {
  return _upb_msg_new(upb_msgdef_layout(m), a);
}

static bool in_oneof(const upb_msglayout_field *field) {
  return field->presence < 0;
}

static uint32_t *oneofcase(const upb_msg *msg,
                           const upb_msglayout_field *field) {
  UPB_ASSERT(in_oneof(field));
  return PTR_AT(msg, ~field->presence, uint32_t);
}

bool upb_msg_has(const upb_msg *msg, const upb_fielddef *f) {
  const upb_msglayout_field *field = upb_fielddef_layout(f);
  UPB_ASSERT(field->presence);
  if (in_oneof(field)) {
    return *oneofcase(msg, field) == field->number;
  } else {
    uint32_t hasbit = field->presence;
    return *PTR_AT(msg, hasbit / 8, char) | (1 << (hasbit % 8));
  }
}

upb_msgval upb_msg_get(const upb_msg *msg, const upb_fielddef *f) {
  const upb_msglayout_field *field = upb_fielddef_layout(f);
  const char *mem = PTR_AT(msg, field->offset, char);
  upb_msgval val;
  if (field->presence == 0 || upb_msg_has(msg, f)) {
    int size = upb_fielddef_isseq(f) ? sizeof(void *)
                                     : field_size[field->descriptortype];
    memcpy(&val, mem, size);
  } else {
    /* TODO(haberman): change upb_fielddef to not require this switch(). */
    switch (upb_fielddef_type(f)) {
      case UPB_TYPE_INT32:
      case UPB_TYPE_ENUM:
        val.int32_val = upb_fielddef_defaultint32(f);
        break;
      case UPB_TYPE_INT64:
        val.int64_val = upb_fielddef_defaultint64(f);
        break;
      case UPB_TYPE_UINT32:
        val.uint32_val = upb_fielddef_defaultuint32(f);
        break;
      case UPB_TYPE_UINT64:
        val.uint64_val = upb_fielddef_defaultuint64(f);
        break;
      case UPB_TYPE_FLOAT:
        val.float_val = upb_fielddef_defaultfloat(f);
        break;
      case UPB_TYPE_DOUBLE:
        val.double_val = upb_fielddef_defaultdouble(f);
        break;
      case UPB_TYPE_BOOL:
        val.double_val = upb_fielddef_defaultbool(f);
        break;
      case UPB_TYPE_STRING:
      case UPB_TYPE_BYTES:
        val.str_val.data = upb_fielddef_defaultstr(f, &val.str_val.size);
        break;
      case UPB_TYPE_MESSAGE:
        val.msg_val = NULL;
        break;
    }
  }
  return val;
}

upb_mutmsgval upb_msg_mutable(upb_msg *msg, const upb_fielddef *f,
                              upb_arena *a) {
  const upb_msglayout_field *field = upb_fielddef_layout(f);
  upb_mutmsgval ret;
  char *mem = PTR_AT(msg, field->offset, char);
  memcpy(&ret, mem, sizeof(void*));
  if (!ret.msg) {
    if (upb_fielddef_ismap(f)) {
      const upb_msgdef *entry = upb_fielddef_msgsubdef(f);
      const upb_fielddef *key = upb_msgdef_itof(entry, UPB_MAPENTRY_KEY);
      const upb_fielddef *value = upb_msgdef_itof(entry, UPB_MAPENTRY_VALUE);
      ret.map = upb_map_new(a, upb_fielddef_type(key), upb_fielddef_type(value));
    } else if (upb_fielddef_isseq(f)) {
      ret.array = upb_array_new(a, upb_fielddef_type(f));
    } else {
      UPB_ASSERT(upb_fielddef_issubmsg(f));
      ret.msg = upb_msg_new(upb_fielddef_msgsubdef(f), a);
    }
    memcpy(mem, &ret, sizeof(void*));
  }
  return ret;
}

void upb_msg_set(upb_msg *msg, const upb_fielddef *f, upb_msgval val,
                 upb_arena *a) {
  const upb_msglayout_field *field = upb_fielddef_layout(f);
  char *mem = PTR_AT(msg, field->offset, char);
  int size = upb_fielddef_isseq(f) ? sizeof(void *)
                                   : field_size[field->descriptortype];
  memcpy(mem, &val, size);
  if (in_oneof(field)) {
    *oneofcase(msg, field) = field->number;
  }
}

#undef DEREF

/** upb_array *****************************************************************/

size_t upb_array_size(const upb_array *arr) {
  return arr->len;
}

upb_msgval upb_array_get(const upb_array *arr, size_t i) {
  upb_msgval ret;
  const char* data = _upb_array_constptr(arr);
  int lg2 = arr->data & 7;
  UPB_ASSERT(i < arr->len);
  memcpy(&ret, data + (i << lg2), 1 << lg2);
  return ret;
}

void upb_array_set(upb_array *arr, size_t i, upb_msgval val) {
  char* data = _upb_array_ptr(arr);
  int lg2 = arr->data & 7;
  UPB_ASSERT(i < arr->len);
  memcpy(data + (i << lg2), &val, 1 << lg2);
}

bool upb_array_append(upb_array *arr, upb_msgval val, upb_arena *arena) {
  if (!_upb_array_realloc(arr, arr->len + 1, arena)) {
    return false;
  }
  arr->len++;
  upb_array_set(arr, arr->len - 1, val);
  return true;
}

/* Resizes the array to the given size, reallocating if necessary, and returns a
 * pointer to the new array elements. */
bool upb_array_resize(upb_array *arr, size_t size, upb_arena *arena) {
  return _upb_array_realloc(arr, size, arena);
}

/** upb_map *******************************************************************/

upb_map *upb_map_new(upb_arena *a, upb_fieldtype_t key_type,
                     upb_fieldtype_t value_type) {
  return _upb_map_new(a, _upb_fieldtype_to_mapsize[key_type],
                      _upb_fieldtype_to_mapsize[value_type]);
}

size_t upb_map_size(const upb_map *map) {
  return _upb_map_size(map);
}

bool upb_map_get(const upb_map *map, upb_msgval key, upb_msgval *val) {
  return _upb_map_get(map, &key, map->key_size, val, map->val_size);
}

bool upb_map_set(upb_map *map, upb_msgval key, upb_msgval val,
                 upb_arena *arena) {
  return _upb_map_set(map, &key, map->key_size, &val, map->val_size, arena);
}

bool upb_map_delete(upb_map *map, upb_msgval key) {
  return _upb_map_delete(map, &key, map->key_size);
}

bool upb_mapiter_next(const upb_map *map, size_t *iter) {
  return _upb_map_next(map, iter);
}

/* Returns the key and value for this entry of the map. */
upb_msgval upb_mapiter_key(const upb_map *map, size_t iter) {
  upb_strtable_iter i = {&map->table, iter};
  upb_strview key = upb_strtable_iter_key(&i);
  upb_msgval ret;
  _upb_map_fromkey(key, &ret, map->key_size);
  return ret;
}

upb_msgval upb_mapiter_value(const upb_map *map, size_t iter) {
  upb_strtable_iter i = {&map->table, iter};
  upb_value val = upb_strtable_iter_value(&i);
  upb_msgval ret;
  _upb_map_fromvalue(val, &ret, map->val_size);
  return ret;
}

/* void upb_mapiter_setvalue(upb_map *map, size_t iter, upb_msgval value); */
