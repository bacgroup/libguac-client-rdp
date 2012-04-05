
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is libguac-client-rdp.
 *
 * The Initial Developer of the Original Code is
 * Michael Jumper.
 * Portions created by the Initial Developer are Copyright (C) 2011
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#ifndef _GUAC_RDP_CLIENT_H
#define _GUAC_RDP_CLIENT_H

#include <freerdp/freerdp.h>
#include <freerdp/codec/color.h>

#include <guacamole/client.h>

#include "rdp_keymap.h"

#define RDP_DEFAULT_PORT 3389

typedef struct guac_rdp_color {
    int red;
    int green;
    int blue;
} guac_rdp_color;

typedef struct rdp_guac_client_data {

    freerdp* rdp_inst;
    rdpSettings* settings;

    int mouse_button_mask;

    guac_rdp_color foreground;
    guac_rdp_color background;

    /**
     * Cairo surface which will receive all drawn glyphs.
     */
    cairo_surface_t* glyph_surface;

    /**
     * Cairo instance for drawing to glyph surface.
     */
    cairo_t* glyph_cairo;

    const guac_layer* current_surface;

    guac_rdp_static_keymap keymap;

    guac_rdp_keysym_state_map keysym_state;

} rdp_guac_client_data;

typedef struct rdp_freerdp_context {

    rdpContext _p;

    guac_client* client;
    CLRCONV* clrconv;

} rdp_freerdp_context;

#endif

