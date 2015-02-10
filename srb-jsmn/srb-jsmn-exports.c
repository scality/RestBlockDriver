/*
 * Copyright (C) 2015 Scality SA - http://www.scality.com
 *
 * This file is part of ScalityRestBlock.
 *
 * ScalityRestBlock is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ScalityRestBlock is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ScalityRestBlock.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <srb-jsmn/srb-jsmn.h>

void srb_jsmn_init(jsmn_parser *parser) {
        jsmn_init(parser);
}
EXPORT_SYMBOL(srb_jsmn_init);

jsmnerr_t srb_jsmn_parse(jsmn_parser *parser, const char *js, size_t len,
                jsmntok_t *tokens, unsigned int num_tokens) {
        return jsmn_parse(parser, js, len, tokens, num_tokens);
}
EXPORT_SYMBOL(srb_jsmn_parse);

MODULE_AUTHOR("Nicolas Trangez <nicolas.trangez@scality.com>");
MODULE_DESCRIPTION("jsmin JSON parsing library");
MODULE_DESCRIPTION("Based on jsmin, copyright (c) 2010 Serge A. Zaitsev");
MODULE_LICENSE("Dual MIT/GPL");
MODULE_VERSION("1.0");
