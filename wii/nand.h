#ifndef NAND_H
#define NAND_H

#include <stdbool.h>

#include "playlog.h"

/* Calls on_session for each play session in the Message Board file on the
   console NAND. Returns false when the file cannot be opened or a read of it
   fails. The sessions before a failed read still reach on_session. */
bool nand_each_session(playlog_session_fn *on_session, void *ctx);

/* A line of text that says why the last call returned false. */
const char *nand_failure(void);

#endif
