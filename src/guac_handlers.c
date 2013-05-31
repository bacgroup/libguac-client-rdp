
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
 *  Matt Hortman
 *  David PHAM-VAN <d.pham-van@ulteo.com> Ulteo SAS
 *  Jocelyn DELALANDE <j.delalande@ulteo.com> Ulteo SAS
 *  Alexandre CONFIANT-LATOUR <a.confiant@ulteo.com> Ulteo SAS
 *
 * Contributions of Ulteo SAS Employees are 
 *   Copyright (C) 2012 Ulteo SAS - http://www.ulteo.com
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

#include <stdlib.h>
#include <string.h>

#include <sys/select.h>
#include <errno.h>

#include <freerdp/freerdp.h>
#include <freerdp/channels/channels.h>
#include <freerdp/input.h>
#include <freerdp/codec/color.h>
#include <freerdp/cache/cache.h>
#include <freerdp/utils/event.h>
#include <freerdp/utils/memory.h>
#include <freerdp/plugins/cliprdr.h>

#include <guacamole/socket.h>
#include <guacamole/protocol.h>
#include <guacamole/client.h>
#include <guacamole/error.h>

#include "client.h"
#include "rdp_keymap.h"
#include "rdp_cliprdr.h"
#include "rdp_printrdr.h"
#include "rdp_seamrdp.h"
#include "rdp_ovdapp.h"
#include "guac_handlers.h"
#include "unicode_convtable.h"

void __guac_rdp_update_keysyms(guac_client* client, const int* keysym_string, int from, int to);
int __guac_rdp_send_keysym(guac_client* client, int keysym, int pressed);


int rdp_guac_client_free_handler(guac_client* client) {

    rdp_guac_client_data* guac_client_data =
        (rdp_guac_client_data*) client->data;

    freerdp* rdp_inst = guac_client_data->rdp_inst;
    rdpChannels* channels = rdp_inst->context->channels;

    /* Clean up RDP client */
	freerdp_channels_close(channels, rdp_inst);
	freerdp_channels_free(channels);
	freerdp_disconnect(rdp_inst);
    freerdp_clrconv_free(((rdp_freerdp_context*) rdp_inst->context)->clrconv);
    cache_free(rdp_inst->context->cache);
    freerdp_free(rdp_inst);

    /* Free client data */
    cairo_surface_destroy(guac_client_data->opaque_glyph_surface);
    cairo_surface_destroy(guac_client_data->trans_glyph_surface);
    free(guac_client_data->clipboard);
    free(guac_client_data);

    return 0;

}

int rdp_guac_client_handle_messages(guac_client* client) {

    rdp_guac_client_data* guac_client_data = ((rdp_guac_client_data*)(client->data));
    freerdp* rdp_inst = guac_client_data->rdp_inst;
    rdpChannels* channels = rdp_inst->context->channels;

    int index;
    int max_fd, fd;
	// Actualy stores file-descriptors, not pointers
    void* read_fds[32];
    void* write_fds[32];
    int read_count = 0;
    int write_count = 0;
    fd_set rfds, wfds;
    RDP_EVENT* event;

    struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = 250000
    };

    /* get rdp fds */
    if (!freerdp_get_fds(rdp_inst, read_fds, &read_count, write_fds, &write_count)) {
        guac_error = GUAC_STATUS_BAD_STATE;
        guac_error_message = "Unable to read RDP file descriptors";
        return 1;
    }

    /* get channel fds */
    if (!freerdp_channels_get_fds(channels, rdp_inst, read_fds, &read_count, write_fds, &write_count)) {
        guac_error = GUAC_STATUS_BAD_STATE;
        guac_error_message = "Unable to read RDP channel file descriptors";
        return 1;
    }

    /* Construct read fd_set */
    max_fd = 0;
    FD_ZERO(&rfds);
    for (index = 0; index < read_count; index++) {
        fd = (int)(long) (read_fds[index]);
        if (fd > max_fd)
            max_fd = fd;
        FD_SET(fd, &rfds);
    }

	/* Adds Ulteo-printing notification FD */
	fd = (int)(long) (guac_client_data->printjob_notif_fifo);
	if (fd > max_fd)
		max_fd = fd;
	
	FD_SET(fd, &rfds);

    /* Construct write fd_set */
    FD_ZERO(&wfds);
    for (index = 0; index < write_count; index++) {
        fd = (int)(long) (write_fds[index]);
        if (fd > max_fd)
            max_fd = fd;
        FD_SET(fd, &wfds);
    }

    /* If no file descriptors, error */
    if (max_fd == 0) {
        guac_error = GUAC_STATUS_BAD_STATE;
        guac_error_message = "No file descriptors";
        return 1;
    }

    /* Otherwise, wait for file descriptors given */
	fd = select(max_fd + 1, &rfds, &wfds, NULL, &timeout);

    if (fd  == -1) {
        /* these are not really errors */
        if (!((errno == EAGAIN) ||
            (errno == EWOULDBLOCK) ||
            (errno == EINPROGRESS) ||
            (errno == EINTR))) /* signal occurred */
        {
            guac_error = GUAC_STATUS_SEE_ERRNO;
            guac_error_message = "Error waiting for file descriptor";
            return 1;
        }
    }

    /* Check the libfreerdp fds */
    if (!freerdp_check_fds(rdp_inst)) {
        guac_error = GUAC_STATUS_BAD_STATE;
        guac_error_message = "Error handling RDP file descriptors";
        return 1;
    }

    /* Check channel fds */
    if (!freerdp_channels_check_fds(channels, rdp_inst)) {
        guac_error = GUAC_STATUS_BAD_STATE;
        guac_error_message = "Error handling RDP channel file descriptors";
        return 1;
    }

    /* Check for channel events */
    event = freerdp_channels_pop_event(channels);
    if (event) {
        /* Handle clipboard events */
        if (event->event_class == RDP_EVENT_CLASS_CLIPRDR)
            guac_rdp_process_cliprdr_event(client, event);
        if (event->event_class == RDP_EVENT_CLASS_SEAMRDP)
						guac_rdp_process_seamrdp_event(client, event);
				if (event->event_class == RDP_EVENT_CLASS_OVDAPP)
						guac_rdp_process_ovdapp_event(client, event);
        freerdp_event_free(event);

    }

    /* Handle RDP disconnect */
    if (freerdp_shall_disconnect(rdp_inst)) {
        guac_error = GUAC_STATUS_NO_INPUT;
        guac_error_message = "RDP server closed connection";
        return 1;
    }

	/* Handle PDF Printjob availability message */
	if (FD_ISSET(guac_client_data->printjob_notif_fifo, &rfds)) {
		guac_rdp_process_printing_notification(client, guac_client_data->printjob_notif_fifo);
		guac_client_log_info(client, "processed");
	}

    /* Success */
    return 0;

}

int rdp_guac_client_mouse_handler(guac_client* client, int x, int y, int mask) {

    rdp_guac_client_data* guac_client_data = (rdp_guac_client_data*) client->data;
    freerdp* rdp_inst = guac_client_data->rdp_inst;

    /* If button mask unchanged, just send move event */
    if (mask == guac_client_data->mouse_button_mask)
        rdp_inst->input->MouseEvent(rdp_inst->input, PTR_FLAGS_MOVE, x, y);

    /* Otherwise, send events describing button change */
    else {

        /* Mouse buttons which have JUST become released */
        int released_mask =  guac_client_data->mouse_button_mask & ~mask;

        /* Mouse buttons which have JUST become pressed */
        int pressed_mask  = ~guac_client_data->mouse_button_mask &  mask;

        /* Release event */
        if (released_mask & 0x07) {

            /* Calculate flags */
            int flags = 0;
            if (released_mask & 0x01) flags |= PTR_FLAGS_BUTTON1;
            if (released_mask & 0x02) flags |= PTR_FLAGS_BUTTON3;
            if (released_mask & 0x04) flags |= PTR_FLAGS_BUTTON2;

            rdp_inst->input->MouseEvent(rdp_inst->input, flags, x, y);

        }

        /* Press event */
        if (pressed_mask & 0x07) {

            /* Calculate flags */
            int flags = PTR_FLAGS_DOWN;
            if (pressed_mask & 0x01) flags |= PTR_FLAGS_BUTTON1;
            if (pressed_mask & 0x02) flags |= PTR_FLAGS_BUTTON3;
            if (pressed_mask & 0x04) flags |= PTR_FLAGS_BUTTON2;
            if (pressed_mask & 0x08) flags |= PTR_FLAGS_WHEEL | 0x78;
            if (pressed_mask & 0x10) flags |= PTR_FLAGS_WHEEL | PTR_FLAGS_WHEEL_NEGATIVE | 0x88;

            /* Send event */
            rdp_inst->input->MouseEvent(rdp_inst->input, flags, x, y);

        }

        /* Scroll event */
        if (pressed_mask & 0x18) {

            /* Down */
            if (pressed_mask & 0x08)
                rdp_inst->input->MouseEvent(
                        rdp_inst->input,
                        PTR_FLAGS_WHEEL | 0x78,
                        x, y);

            /* Up */
            if (pressed_mask & 0x10)
                rdp_inst->input->MouseEvent(
                        rdp_inst->input,
                        PTR_FLAGS_WHEEL | PTR_FLAGS_WHEEL_NEGATIVE | 0x88,
                        x, y);

        }


        guac_client_data->mouse_button_mask = mask;
    }

    return 0;
}


int __guac_rdp_send_keysym(guac_client* client, int keysym, int pressed) {

    rdp_guac_client_data* guac_client_data = (rdp_guac_client_data*) client->data;
    freerdp* rdp_inst = guac_client_data->rdp_inst;

    /* If keysym can be in lookup table */
    if (keysym <= 0xFFFF) {

        /* Look up scancode mapping */
        const guac_rdp_keysym_desc* keysym_desc =
            &GUAC_RDP_KEYSYM_LOOKUP(guac_client_data->keymap, keysym);

        /* If defined, send event */
        if (keysym_desc->scancode != 0) {

            /* If defined, send any prerequesite keys that must be set */
            if (keysym_desc->set_keysyms != NULL)
                __guac_rdp_update_keysyms(client, keysym_desc->set_keysyms, 0, 1);

            /* If defined, release any keys that must be cleared */
            if (keysym_desc->clear_keysyms != NULL)
                __guac_rdp_update_keysyms(client, keysym_desc->clear_keysyms, 1, 0);

            /* Send actual key */
            rdp_inst->input->KeyboardEvent(
					rdp_inst->input,
                    keysym_desc->flags
					    | (pressed ? KBD_FLAGS_DOWN : KBD_FLAGS_RELEASE),
					keysym_desc->scancode);

			//guac_client_log_info(client, "Base flags are %d", keysym_desc->flags);

            /* If defined, release any keys that were originally released */
            if (keysym_desc->set_keysyms != NULL)
                __guac_rdp_update_keysyms(client, keysym_desc->set_keysyms, 0, 0);

            /* If defined, send any keys that were originally set */
            if (keysym_desc->clear_keysyms != NULL)
                __guac_rdp_update_keysyms(client, keysym_desc->clear_keysyms, 1, 1);

            return 0;

        }
	}
    /* Fall back to unicode events if undefined inside current keymap */

    /* Only send when key pressed - Unicode events do not have
     * DOWN/RELEASE flags */
    if (pressed) {

        /* Translate keysym into codepoint */
        int codepoint;
        if (keysym <= 0xFF)
            codepoint = keysym;
        else if (keysym >= 0x1000000)
            codepoint = keysym & 0xFFFFFF;
        else {
            guac_client_log_info(client,
                    "Unmapped keysym has no equivalent unicode "
                    "value: 0x%x", keysym);
            return 0;
        }

		/*
        guac_client_log_info(client, "Translated keysym 0x%x to U+%04X",
                keysym, codepoint);
		//*/

        /* Send Unicode event */
        rdp_inst->input->UnicodeKeyboardEvent(
                rdp_inst->input,
                0, codepoint);
    }
    /*
    else
        guac_client_log_info(client, "Ignoring key release (Unicode event)");
	//*/
    return 0;
}

int __guac_rdp_send_unicode(guac_client* client, int unicode) {
    rdp_guac_client_data* guac_client_data = (rdp_guac_client_data*) client->data;
    freerdp* rdp_inst = guac_client_data->rdp_inst;

    /* Send Unicode event */
    rdp_inst->input->UnicodeKeyboardEvent(rdp_inst->input, 0, unicode);
    return 0;
}

void __guac_rdp_update_keysyms(guac_client* client, const int* keysym_string, int from, int to) {

    rdp_guac_client_data* guac_client_data = (rdp_guac_client_data*) client->data;
    int keysym;

    /* Send all keysyms in string, NULL terminated */
    while ((keysym = *keysym_string) != 0) {

        /* Get current keysym state */
        int current_state = GUAC_RDP_KEYSYM_LOOKUP(guac_client_data->keysym_state, keysym);

        /* If key is currently in given state, send event for changing it to specified "to" state */
        if (current_state == from)
            __guac_rdp_send_keysym(client, *keysym_string, to);

        /* Next keysym */
        keysym_string++;

    }

}

int rdp_guac_client_key_handler(guac_client* client, int keysym, int pressed) {

    rdp_guac_client_data* guac_client_data = (rdp_guac_client_data*) client->data;

    switch(pressed) {
        case 0:
        case 1:
            /* Update keysym state */
            GUAC_RDP_KEYSYM_LOOKUP(guac_client_data->keysym_state, keysym) = pressed;
            return __guac_rdp_send_keysym(client, keysym, pressed);
        case 2:
            /* Send unicode symbol */
            return __guac_rdp_send_unicode(client, keysym);
    }
    return 0;
}

static char reverseCode(char c) {
    if (c >= 'A' && c <= 'Z') return (c-'A');
    if (c >= 'a' && c <= 'z') return (c-'a')+26;
    if (c >= '0' && c <= '9') return (c-'0')+52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return 0;
}

char* decode_base64(char* data) {
    char buffer[4];
    int offset;
    int data_len = strlen(data);
    char *output = malloc((data_len*3/4)+1);

    for (offset=0 ; offset < data_len ; offset+=4) {
        int base = offset*3/4;
        buffer[0] = reverseCode(data[offset+0]);
        buffer[1] = reverseCode(data[offset+1]);
        buffer[2] = reverseCode(data[offset+2]);
        buffer[3] = reverseCode(data[offset+3]);

        output[base+0] = (buffer[0] << 2) | (((buffer[1] & 0x30) >> 4) & 0x03);
        output[base+1] = ((buffer[1] & 0x0F) << 4) | (((buffer[2] & 0x3C) >> 2) & 0x0F);
        output[base+2] = ((buffer[2] & 0x03) << 6) | buffer[3];
    }

    output[data_len*3/4]='\0';
    return output;
}

int rdp_guac_client_seamrdp_handler(guac_client* client, char* data) {
	rdp_guac_client_data *client_data = (rdp_guac_client_data*) client->data;
	rdpChannels* channels = client_data->rdp_inst->context->channels;
	RDP_EVENT* event = xnew(RDP_EVENT);
	int bufferLength;
	char *buffer;

	bufferLength = strlen(data);
	buffer = malloc(bufferLength+1);
	strcpy(buffer, data);

	/*printf("%s\n", buffer);*/

	event->event_class = RDP_EVENT_CLASS_SEAMRDP;
	event->event_type = 0;
	event->on_event_free_callback = NULL;
	event->user_data = buffer;

	freerdp_channels_send_event(channels, (RDP_EVENT*) event);
	return 0;
}

int rdp_guac_client_ovdapp_handler(guac_client* client, char* data) {
	rdp_guac_client_data *client_data = (rdp_guac_client_data*) client->data;
	rdpChannels* channels = client_data->rdp_inst->context->channels;
	RDP_EVENT* event = xnew(RDP_EVENT);
	int bufferLength;
	char *buffer;

	bufferLength = strlen(data);
	buffer = malloc(bufferLength+1);
	strcpy(buffer, data);

	/*printf("%s\n", buffer);*/

	event->event_class = RDP_EVENT_CLASS_OVDAPP;
	event->event_type = 0;
	event->on_event_free_callback = NULL;
	event->user_data = buffer;

	freerdp_channels_send_event(channels, (RDP_EVENT*) event);
	return 0;
}

int rdp_guac_client_clipboard_handler(guac_client* client, char* data) {
  
    rdp_guac_client_data *client_data = (rdp_guac_client_data*) client->data;

    rdpChannels* channels = client_data->rdp_inst->context->channels;

    RDP_CB_FORMAT_LIST_EVENT* format_list =
        (RDP_CB_FORMAT_LIST_EVENT*) freerdp_event_new(
            RDP_EVENT_CLASS_CLIPRDR,
            RDP_EVENT_TYPE_CB_FORMAT_LIST,
            NULL, NULL);

    /* Free existing data */
    free(client_data->clipboard);

    /* Store data in client */
    client_data->clipboard = decode_base64(data);
    client_data->clipboard_length = strlen(client_data->clipboard);

    /* Notify server that text data is now available */
    format_list->formats = (uint32*) malloc(sizeof(uint32));
    format_list->formats[0] = CB_FORMAT_UNICODETEXT;
    format_list->num_formats = 1;

    freerdp_channels_send_event(channels, (RDP_EVENT*) format_list);

    return 0;

}

