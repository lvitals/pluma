/*
 * pluma-comment.h
 * This file is part of pluma
 *
 * Copyright (C) 2026 MATE Developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __PLUMA_COMMENT_H__
#define __PLUMA_COMMENT_H__

#include "pluma-document.h"

G_BEGIN_DECLS

void pluma_comment_toggle_line_comment  (PlumaDocument *document);
void pluma_comment_toggle_block_comment (PlumaDocument *document);

G_END_DECLS

#endif /* __PLUMA_COMMENT_H__ */
