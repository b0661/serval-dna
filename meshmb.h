#ifndef __SERVAL_DNA__MESHMB_H
#define __SERVAL_DNA__MESHMB_H

struct meshmb_feeds;

enum meshmb_send_status{
  MESHMB_ERROR = -1,
  MESHMB_OK = 0,

};

struct rhizome_manifest_field_assignment;
int meshmb_send(const keyring_identity *id, const char *message, size_t message_len,
  unsigned nassignments, const struct rhizome_manifest_field_assignment *assignments);

int meshmb_open(keyring_identity *id, struct meshmb_feeds **feeds);
void meshmb_close(struct meshmb_feeds *feeds);

#endif
