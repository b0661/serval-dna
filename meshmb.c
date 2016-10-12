#include "serval.h"
#include "serval_types.h"
#include "dataformats.h"
#include "log.h"
#include "debug.h"
#include "conf.h"
#include "overlay_buffer.h"
#include "keyring.h"
#include "crypto.h"
#include "mem.h"
#include "meshmb.h"

struct feed_metadata{
  unsigned tree_depth;
  struct message_ply ply; // (ply starts with a rhizome_bid_t, so this is consistent with a nibble tree)
  const char *name;
  // what is the offset of their last message
  uint64_t last_message;
  // what is the last message we processed?
  uint64_t last_seen;
  // our cached value for the last known size of their ply
  uint64_t size;
};

struct meshmb_feeds{
  struct tree_root root;
  keyring_identity *id;
};

#define MAX_NAME_LEN (256)  // ??

static void update_stats(struct feed_metadata *metadata, struct message_ply_read *reader)
{
  if (!metadata->ply.found){
    // get the current size from the db
    if (sqlite_exec_uint64(&metadata->ply.size,
      "SELECT filesize FROM manifests WHERE id = ?",
      RHIZOME_BID_T, &metadata->ply.bundle_id,
      END) == 1)
	metadata->ply.found = 1;
    else
      return;
  }

  if (metadata->size == metadata->ply.size)
    return;

  if (!message_ply_is_open(reader)
    && message_ply_read_open(reader, &metadata->ply.bundle_id)!=0)
    return;

  if (metadata->name)
    free((void*)metadata->name);
  metadata->name = reader->name ? str_edup(reader->name) : NULL;

  reader->read.offset = reader->read.length;
  if (message_ply_find_prev(reader, MESSAGE_BLOCK_TYPE_MESSAGE)==0)
    metadata->last_message = reader->record_end_offset;

  metadata->size = metadata->ply.size;
  return;
}

static int write_metadata(void **record, void *context)
{
  struct feed_metadata *metadata = (struct feed_metadata *)*record;
  struct rhizome_write *write = (struct rhizome_write *)context;

  assert(metadata->size >= metadata->last_message);
  assert(metadata->size >= metadata->last_seen);
  {
    struct message_ply_read reader;
    bzero(&reader, sizeof(reader));
    update_stats(metadata, &reader);
  }
  unsigned name_len = (metadata->name ? strlen(metadata->name) : 0) + 1;
  if (name_len > MAX_NAME_LEN)
    name_len = MAX_NAME_LEN;
  uint8_t buffer[sizeof (rhizome_bid_t) + 1 + 12*3 + name_len];
  bcopy(metadata->ply.bundle_id.binary, buffer, sizeof (rhizome_bid_t));
  size_t len = sizeof (rhizome_bid_t);
  buffer[len++]=0;// flags?
  len+=pack_uint(&buffer[len], metadata->size);
  len+=pack_uint(&buffer[len], metadata->size - metadata->last_message);
  len+=pack_uint(&buffer[len], metadata->size - metadata->last_seen);
  if (name_len > 1)
    strncpy_nul((char *)&buffer[len], metadata->name, name_len);
  else
    buffer[len]=0;
  len+=name_len;
  assert(len < sizeof buffer);
  return rhizome_write_buffer(write, buffer, len);
}

int meshmb_flush(struct meshmb_feeds *feeds)
{

  rhizome_manifest *m = rhizome_new_manifest();
  if (!m)
    return -1;

  int ret =-1;
  sign_keypair_t key;
  crypto_seed_keypair(&key,
    "91656c3d62e9fe2678a1a81fabe3f413%s5a37120ca55d911634560e4d4dc1283f",
    alloca_tohex(feeds->id->sign_keypair->private_key.binary, sizeof feeds->id->sign_keypair->private_key));
  struct rhizome_bundle_result result = rhizome_private_bundle(m, &key);

  switch(result.status){
    case RHIZOME_BUNDLE_STATUS_SAME:
    case RHIZOME_BUNDLE_STATUS_NEW:
    {
      struct rhizome_write write;
      bzero(&write, sizeof(write));

      enum rhizome_payload_status pstatus = rhizome_write_open_manifest(&write, m);
      if (pstatus==RHIZOME_PAYLOAD_STATUS_NEW){
	if (tree_walk(&feeds->root, NULL, 0, write_metadata, &write)==0){
	  pstatus = rhizome_finish_write(&write);
	  if (pstatus == RHIZOME_PAYLOAD_STATUS_NEW)
	    ret = 0;
	}
      }
      if (ret!=0)
	rhizome_fail_write(&write);
      break;
    }
    default:
      break;
  }

  rhizome_manifest_free(m);
  return ret;
}

static int free_feed(void **record, void *UNUSED(context))
{
  struct feed_metadata *f = *record;
  if (f->name)
    free((void *)f->name);
  free(f);
  *record = NULL;
  return 0;
}

void meshmb_close(struct meshmb_feeds *feeds)
{
  tree_walk(&feeds->root, NULL, 0, free_feed, NULL);
  free(feeds);
}

static void* alloc_feed (void *UNUSED(context), const uint8_t *binary, size_t UNUSED(bin_length))
{
  struct feed_metadata *feed = emalloc_zero(sizeof(struct feed_metadata));
  if (feed)
    feed->ply.bundle_id = *(rhizome_bid_t *)binary;
  return feed;
}

static int read_metadata(struct meshmb_feeds *feeds, struct rhizome_read *read)
{
  struct rhizome_read_buffer buff;
  bzero(&buff, sizeof(buff));
  uint8_t buffer[sizeof (rhizome_bid_t) + 12*3 + MAX_NAME_LEN];

  uint8_t version=0xFF;
  if (rhizome_read_buffered(read, &buff, &version, 1)==-1)
    return -1;

  if (version != 0)
    return WHYF("Unknown file format version (got 0x%02x)", version);

  while(1){
    ssize_t bytes = rhizome_read_buffered(read, &buff, buffer, sizeof buffer);
    if (bytes==0)
      break;

    uint64_t delta=0;
    uint64_t size;
    uint64_t last_message;
    uint64_t last_seen;
    int unpacked;
    const rhizome_bid_t *bid = (const rhizome_bid_t *)&buffer[0];
    unsigned offset = sizeof(rhizome_bid_t);
    if (offset >= (unsigned)bytes)
      return -1;
    //uint8_t flags = buffer[offset++];
    offset++;
    if (offset >= (unsigned)bytes)
      return -1;

    if ((unpacked = unpack_uint(buffer+offset, bytes-offset, &size)) == -1)
      return -1;
    offset += unpacked;

    if ((unpacked = unpack_uint(buffer+offset, bytes-offset, &delta)) == -1)
      return -1;
    offset += unpacked;
    last_message = size - delta;

    if ((unpacked = unpack_uint(buffer+offset, bytes-offset, &delta)) == -1)
      return -1;
    offset += unpacked;
    last_seen = size - delta;

    const char *name = (const char *)&buffer[offset];
    while(buffer[offset++]){
      if (offset >= (unsigned)bytes)
	return -1;
    }

    read->offset += offset - bytes;
    struct feed_metadata *result;
    if (tree_find(&feeds->root, (void**)&result, bid->binary, sizeof *bid, alloc_feed, NULL)<0)
      return -1;

    result->last_message = last_message;
    result->last_seen = last_seen;
    result->size = size;
    result->name = (name && *name) ? str_edup(name) : NULL;
  }
  return 0;
}

int meshmb_open(keyring_identity *id, struct meshmb_feeds **feeds)
{
  int ret = -1;

  *feeds = emalloc_zero(sizeof(struct meshmb_feeds));
  if (*feeds){
    (*feeds)->id = id;
    rhizome_manifest *m = rhizome_new_manifest();
    if (m){
      sign_keypair_t key;
      crypto_seed_keypair(&key,
	"91656c3d62e9fe2678a1a81fabe3f413%s5a37120ca55d911634560e4d4dc1283f",
	alloca_tohex(id->sign_keypair->private_key.binary, sizeof id->sign_keypair->private_key));
      struct rhizome_bundle_result result = rhizome_private_bundle(m, &key);
      switch(result.status){
	case RHIZOME_BUNDLE_STATUS_SAME:{
	  struct rhizome_read read;
	  bzero(&read, sizeof(read));

	  enum rhizome_payload_status pstatus = rhizome_open_decrypt_read(m, &read);
	  if (pstatus == RHIZOME_PAYLOAD_STATUS_STORED){
	    if (read_metadata(*feeds, &read)==-1)
	      WHYF("Failed to read metadata");
	    else
	      ret = 0;
	  }else
	    WHYF("Failed to read metadata: %s", rhizome_payload_status_message(pstatus));

	  rhizome_read_close(&read);
	}break;

	case RHIZOME_BUNDLE_STATUS_NEW:
	  ret = 0;
	  break;

	case RHIZOME_BUNDLE_STATUS_BUSY:
	  break;

	default:
	  // everything else should be impossible.
	  FATALF("Cannot create manifest: %s", alloca_rhizome_bundle_result(result));
      }

      rhizome_bundle_result_free(&result);
    }

    rhizome_manifest_free(m);
  }

  if (ret!=0){
    meshmb_close(*feeds);
    *feeds=NULL;
  }
  return ret;
}

int meshmb_send(const keyring_identity *id, const char *message, size_t message_len,
  unsigned nassignments, const struct rhizome_manifest_field_assignment *assignments){

  const char *did=NULL, *name=NULL;
  struct message_ply ply;
  bzero(&ply, sizeof ply);

  ply.bundle_id = id->sign_keypair->public_key;
  ply.known_bid = 1;

  struct overlay_buffer *b = ob_new();
  message_ply_append_message(b, message, message_len);
  message_ply_append_timestamp(b);
  assert(!ob_overrun(b));

  keyring_identity_extract(id, &did, &name);
  int ret = message_ply_append(id, RHIZOME_SERVICE_MESHMB, NULL, &ply, b, name, nassignments, assignments);
  ob_free(b);

  return ret;
}
