
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
 *  David PHAM-VAN <d.pham-van@ulteo.com> Ulteo SAS - http://www.ulteo.com
 *  David LECHEVALIER <david@ulteo.com> Ulteo SAS - http://www.ulteo.com - 2012
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
#include <guacamole/protocol.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <dirent.h>
#include <errno.h>

#include <freerdp/freerdp.h>
#include <freerdp/channels/channels.h>
#include <freerdp/utils/event.h>
#include <freerdp/utils/unicode.h>
#include <freerdp/utils/memory.h>


#include <guacamole/client.h>


#include "client.h"
#include "rdp_printrdr.h"

// C doesn't have any way to remove recursivly a directory...
int rmrf(const char *path) {
	DIR *d = opendir(path);
	size_t path_len = strlen(path);
	int r = -1;
	if (d) {
		struct dirent *p;
		r = 0;
		while (!r && (p=readdir(d))) {
			int r2 = -1;
			char *buf;
			size_t len;
				
			/* Skip the names "." and ".." as we don't want to recurse on them. */
			if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, "..")) {
				continue;
			}

			len = path_len + strlen(p->d_name) + 2; 
			buf = malloc(len);
					
			if (buf) {
				struct stat statbuf;
				snprintf(buf, len, "%s/%s", path, p->d_name);
						
				if (!stat(buf, &statbuf)) {
					if (S_ISDIR(statbuf.st_mode)) {
						r2 = rmrf(buf);
					}
					else {
						r2 = unlink(buf);
					}
				}
				free(buf);
			}
			r = r2;
		}
		closedir(d);
	}
	if (!r)	{
		r = rmdir(path);
	}
	return r;
}

/**
 * Prepares the spooling directory and fifo for communication with freerdp.
 *
 * Prepares the dir to hold PDFs of printjob and a FIFO.
 * Communication is made with FreeRDP module printer_ulteo_pdf.
 */
int guac_rdp_prepare_ulteo_printing(freerdp* instance, // in args
									int* p_fifo        // out args
									) {
	char spool_fifo[MAX_PDF_PRINTJOB_NAME_LEN];
	char session_spool_path[MAX_PDF_PRINTJOB_NAME_LEN];
	guac_client* client = ((rdp_freerdp_context*)(instance->context))->client;

	sprintf(session_spool_path, "%s/%s",
			FREERDP_ULTEO_SPOOL_PATH, instance->settings->username);


	sprintf(spool_fifo, "%s/fifo", session_spool_path);

	guac_client_log_info(client, "Preparing printing support, will spool on FIFO %s", 
						 spool_fifo);

	if (mkdir(session_spool_path, 0755)) {
		if (errno == EEXIST) {
			rmrf(session_spool_path);
			mkdir(session_spool_path, S_IRWXU);
		} else {
			perror("Cannot create session spool dir: ");
			return -1;
		}
	}
	
	if (mkfifo(spool_fifo, S_IRWXU) != 0) {
		perror("Cannot create spool fifo: ");
		return -2;
	}
	
	// Opened in nonblock mode, instead, we will block forever on open()
	*p_fifo = open(spool_fifo, O_RDONLY|O_NONBLOCK);
	if (*p_fifo <= 0) {
		perror("Cannot open FIFO: ");
		return -3;
	}

	return 0;
}


/**
 * Transform a local filename into an URL to fetch the URL.
 */
void job_path_to_url(char* job_url,  const char * job_path) {
	// URL example : "/printer/job/2/get\n"
	// Base URL
	strcpy(job_url, "/printer/job/");
	// Append the job ID
	strcat(job_url, rindex(job_path, '/'));
	// Remove the '.pdf'
	*(rindex(job_url, '.')) = '\0';
	// Append '/get'
	strcat(job_url, "/get");
}


void guac_rdp_process_printing_notification(guac_client* client, int fd) {
	char pdf_filename[MAX_PDF_PRINTJOB_NAME_LEN];
	char pdf_url[MAX_PDF_PRINTJOB_NAME_LEN];

	if (read(fd, pdf_filename, MAX_PDF_PRINTJOB_NAME_LEN) <= 0) {
		guac_client_log_info(client, "Read 0 bytes from FIFO...");
		return;
	}

	job_path_to_url(pdf_url, pdf_filename);
	guac_client_log_info(client, "Sending printing notification to client (%s)", pdf_url);
	guac_protocol_send_pdf_printjob_notif(client->socket, pdf_url);
}
