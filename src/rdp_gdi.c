
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
 * Matt Hortman
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

#include <freerdp/freerdp.h>

#include <guacamole/client.h>

#include "client.h"
#include "rdp_bitmap.h"

static int __guac_rdp_clip_rect(rdp_guac_client_data* data, int* x, int* y, int* w, int* h) {

    if (data->bounded) {

        /* Get rect coordinates */
        int clipped_left   = *x;
        int clipped_top    = *y;
        int clipped_right  = clipped_left + *w - 1;
        int clipped_bottom = clipped_top  + *h - 1;

        /* Clip left */
        if      (clipped_left < data->bounds_left)  clipped_left = data->bounds_left;
        else if (clipped_left > data->bounds_right) return 1;

        /* Clip right */
        if      (clipped_right < data->bounds_left)  return 1;
        else if (clipped_right > data->bounds_right) clipped_right = data->bounds_right;

        /* Clip top */
        if      (clipped_top < data->bounds_top)    clipped_top = data->bounds_top;
        else if (clipped_top > data->bounds_bottom) return 1;

        /* Clip bottom */
        if      (clipped_bottom < data->bounds_top)    return 1;
        else if (clipped_bottom > data->bounds_bottom) clipped_bottom = data->bounds_bottom;

        /* Store new rect dimensions */
        *x = clipped_left;
        *y = clipped_top;
        *w = clipped_right  - clipped_left + 1;
        *h = clipped_bottom - clipped_top  + 1;

    }

    return 0;

}

guac_transfer_function guac_rdp_rop3_transfer_function(guac_client* client,
        int rop3) {

    /* Translate supported ROP3 opcodes into composite modes */
    switch (rop3) {

        /* "DSon" !(src | dest) */
        case 0x11: return GUAC_TRANSFER_BINARY_NOR;

        /* "DSna" !src & dest */
        case 0x22: return GUAC_TRANSFER_BINARY_NSRC_AND;

        /* "Sn" !src */
        case 0x33: return GUAC_TRANSFER_BINARY_NSRC;

        /* "SDna" (src & !dest) */
        case 0x44: return GUAC_TRANSFER_BINARY_NDEST_AND;

        /* "Dn" !dest */
        case 0x55: return GUAC_TRANSFER_BINARY_NDEST;

        /* "SRCINVERT" (src ^ dest) */
        case 0x66: return GUAC_TRANSFER_BINARY_XOR;

        /* "DSan" !(src & dest) */
        case 0x77: return GUAC_TRANSFER_BINARY_NAND;

        /* "SRCAND" (src & dest) */
        case 0x88: return GUAC_TRANSFER_BINARY_AND;

        /* "DSxn" !(src ^ dest) */
        case 0x99: return GUAC_TRANSFER_BINARY_XNOR;

        /* "MERGEPAINT" (!src | dest)*/
        case 0xBB: return GUAC_TRANSFER_BINARY_NSRC_OR;

        /* "SDno" (src | !dest) */
        case 0xDD: return GUAC_TRANSFER_BINARY_NDEST_OR;

        /* "SRCPAINT" (src | dest) */
        case 0xEE: return GUAC_TRANSFER_BINARY_OR;

        /* 0x00 = "BLACKNESS" (0) */
        /* 0xAA = "NOP" (dest) */
        /* 0xCC = "SRCCOPY" (src) */
        /* 0xFF = "WHITENESS" (1) */

    }

    /* Log warning if ROP3 opcode not supported */
    guac_client_log_info (client, "guac_rdp_rop3_transfer_function: "
            "UNSUPPORTED opcode = 0x%02X", rop3);

    /* Default to BINARY_SRC */
    return GUAC_TRANSFER_BINARY_SRC;

}

void guac_rdp_gdi_dstblt(rdpContext* context, DSTBLT_ORDER* dstblt) {

    guac_client* client = ((rdp_freerdp_context*) context)->client;
    const guac_layer* current_layer = ((rdp_guac_client_data*) client->data)->current_surface;
    
    int x = dstblt->nLeftRect;
    int y = dstblt->nTopRect;
    int w = dstblt->nWidth;
    int h = dstblt->nHeight;

    /* Clip operation to bounds */
    if (__guac_rdp_clip_rect(client->data, &x, &y, &w, &h))
        return;

    switch (dstblt->bRop) {

        /* Blackness */
        case 0:

            /* Send black rectangle */
            guac_protocol_send_rect(client->socket, current_layer, x, y, w, h);

            guac_protocol_send_cfill(client->socket,
                    GUAC_COMP_OVER, current_layer,
                    0, 0, 0, 255);

            break;
            
       /* DSTINVERT */
        case 0x55:

            /* Invert */
            guac_protocol_send_transfer(client->socket,
                    current_layer, x, y, w, h,
                    GUAC_TRANSFER_BINARY_NDEST,
                    current_layer, x, y);

            break;

        /* NOP */
        case 0xAA:
            break;

        /* Whiteness */
        case 0xFF:
            guac_protocol_send_rect(client->socket, current_layer, x, y, w, h);

            guac_protocol_send_cfill(client->socket,
                    GUAC_COMP_OVER, current_layer,
                    0xFF, 0xFF, 0xFF, 0xFF);
            break;          

        /* Unsupported ROP3 */
        default:
            guac_client_log_info(client,
                    "guac_rdp_gdi_dstblt(rop3=%i)", dstblt->bRop);

    }



}

void guac_rdp_gdi_patblt(rdpContext* context, PATBLT_ORDER* patblt) {
    guac_client* client = ((rdp_freerdp_context*) context)->client;
    guac_client_log_info(client, "guac_rdp_gdi_patblt()");
}

void guac_rdp_gdi_scrblt(rdpContext* context, SCRBLT_ORDER* scrblt) {

    guac_client* client = ((rdp_freerdp_context*) context)->client;
    const guac_layer* current_layer = ((rdp_guac_client_data*) client->data)->current_surface;
    
    int x = scrblt->nLeftRect;
    int y = scrblt->nTopRect;
    int w = scrblt->nWidth;
    int h = scrblt->nHeight;
    
    int x_src = scrblt->nXSrc;
    int y_src = scrblt->nYSrc;

    /* Clip operation to bounds */
    if (__guac_rdp_clip_rect(client->data, &x, &y, &w, &h))
        return;
    
    /* Update source coordinates */
    x_src += x - scrblt->nLeftRect;
    y_src += y - scrblt->nTopRect;

    /* Copy screen rect to current surface */
    guac_protocol_send_copy(client->socket,
            GUAC_DEFAULT_LAYER, x_src, y_src, w, h,
            GUAC_COMP_OVER, current_layer, x, y);

}

void guac_rdp_gdi_memblt(rdpContext* context, MEMBLT_ORDER* memblt) {

    guac_client* client = ((rdp_freerdp_context*) context)->client;
    const guac_layer* current_layer = ((rdp_guac_client_data*) client->data)->current_surface;
    guac_socket* socket = client->socket;
    guac_rdp_bitmap* bitmap = (guac_rdp_bitmap*) memblt->bitmap;
    
    int x = memblt->nLeftRect;
    int y = memblt->nTopRect;
    int w = memblt->nWidth;
    int h = memblt->nHeight;
    
    int x_src = memblt->nXSrc;
    int y_src = memblt->nYSrc;

    /* Clip operation to bounds */
    if (__guac_rdp_clip_rect(client->data, &x, &y, &w, &h))
        return;
    
    /* Update source coordinates */
    x_src += x - memblt->nLeftRect;
    y_src += y - memblt->nTopRect;

    switch (memblt->bRop) {

        /* If blackness, send black rectangle */
        case 0x00:
            guac_protocol_send_rect(client->socket, current_layer, x, y, w, h);

            guac_protocol_send_cfill(client->socket,
                    GUAC_COMP_OVER, current_layer,
                    0x00, 0x00, 0x00, 0xFF);
            break;

        /* If NOP, do nothing */
        case 0xAA:
            break;

        /* If operation is just SRC, simply copy */
        case 0xCC: 

            /* If not cached, cache if necessary */
            if (((guac_rdp_bitmap*) bitmap)->layer == NULL
                    && ((guac_rdp_bitmap*) bitmap)->used >= 1)
                guac_rdp_cache_bitmap(context, memblt->bitmap);

            /* If not cached, send as PNG */
            if (bitmap->layer == NULL) {
                if (memblt->bitmap->data != NULL) {

                    /* Create surface from image data */
                    cairo_surface_t* surface = cairo_image_surface_create_for_data(
                        memblt->bitmap->data + 4*(x_src + y_src*memblt->bitmap->width),
                        CAIRO_FORMAT_RGB24, w, h, 4*memblt->bitmap->width);

                    /* Send surface to buffer */
                    guac_protocol_send_png(socket,
                            GUAC_COMP_OVER, current_layer,
                            x, y, surface);

                    /* Free surface */
                    cairo_surface_destroy(surface);

                }
            }

            /* Otherwise, copy */
            else
                guac_protocol_send_copy(socket,
                        bitmap->layer, x_src, y_src, w, h,
                        GUAC_COMP_OVER, current_layer, x, y);

            /* Increment usage counter */
            ((guac_rdp_bitmap*) bitmap)->used++;

            break;

        /* If whiteness, send white rectangle */
        case 0xFF:
            guac_protocol_send_rect(client->socket, current_layer, x, y, w, h);

            guac_protocol_send_cfill(client->socket,
                    GUAC_COMP_OVER, current_layer,
                    0xFF, 0xFF, 0xFF, 0xFF);
            break;

        /* Otherwise, use transfer */
        default:

            /* If not available as a surface, make available. */
            if (bitmap->layer == NULL)
                guac_rdp_cache_bitmap(context, memblt->bitmap);

            guac_protocol_send_transfer(socket,
                    bitmap->layer, x_src, y_src, w, h,
                    guac_rdp_rop3_transfer_function(client, memblt->bRop),
                    current_layer, x, y);

            /* Increment usage counter */
            ((guac_rdp_bitmap*) bitmap)->used++;

    }

}

void guac_rdp_gdi_opaquerect(rdpContext* context, OPAQUE_RECT_ORDER* opaque_rect) {

    guac_client* client = ((rdp_freerdp_context*) context)->client;
    uint32 color = freerdp_color_convert_var(opaque_rect->color,
            context->instance->settings->color_depth, 32,
            ((rdp_freerdp_context*) context)->clrconv);

    const guac_layer* current_layer = ((rdp_guac_client_data*) client->data)->current_surface;

    int x = opaque_rect->nLeftRect;
    int y = opaque_rect->nTopRect;
    int w = opaque_rect->nWidth;
    int h = opaque_rect->nHeight;

    /* Clip operation to bounds */
    if (__guac_rdp_clip_rect(client->data, &x, &y, &w, &h))
        return;

    guac_protocol_send_rect(client->socket, current_layer, x, y, w, h);

    guac_protocol_send_cfill(client->socket,
            GUAC_COMP_OVER, current_layer,
            (color >> 16) & 0xFF,
            (color >> 8 ) & 0xFF,
            (color      ) & 0xFF,
            255);

}

void guac_rdp_gdi_palette_update(rdpContext* context, PALETTE_UPDATE* palette) {

    CLRCONV* clrconv = ((rdp_freerdp_context*) context)->clrconv;
    clrconv->palette->count = palette->number;
    clrconv->palette->entries = palette->entries;

}

void guac_rdp_gdi_set_bounds(rdpContext* context, rdpBounds* bounds) {

    guac_client* client = ((rdp_freerdp_context*) context)->client;
    const guac_layer* current_layer = ((rdp_guac_client_data*) client->data)->current_surface;

    /* Reset clip */
    guac_protocol_send_reset(client->socket, current_layer);

    /* Set clip if specified */
    if (bounds != NULL) {
        guac_protocol_send_rect(client->socket, current_layer,
                bounds->left, bounds->top,
                bounds->right - bounds->left + 1,
                bounds->bottom - bounds->top + 1);

        guac_protocol_send_clip(client->socket, current_layer);
    }

    /* If no bounds given, clear bounding rect */
    if (bounds == NULL)
        ((rdp_guac_client_data*)client->data)->bounded = false;

    /* Otherwise, set bounding rectangle */
    else {
        ((rdp_guac_client_data*)client->data)->bounded = true;
        ((rdp_guac_client_data*)client->data)->bounds_left   = bounds->left;
        ((rdp_guac_client_data*)client->data)->bounds_top    = bounds->top;
        ((rdp_guac_client_data*)client->data)->bounds_right  = bounds->right;
        ((rdp_guac_client_data*)client->data)->bounds_bottom = bounds->bottom;    
    }

}

void guac_rdp_gdi_end_paint(rdpContext* context) {
    guac_client* client = ((rdp_freerdp_context*) context)->client;
    guac_socket_flush(client->socket);
}

