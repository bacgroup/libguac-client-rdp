/**
 * Copyright 2012 Ulteo SAS
 * http://www.ulteo.com
 * Author Alexandre CONFIANT-LATOUR <a.confiant@ulteo.com> 2012
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 **/

#include <freerdp/freerdp.h>
#include <freerdp/channels/channels.h>
#include <freerdp/utils/event.h>
#include <freerdp/utils/memory.h>

#include <guacamole/client.h>

#include "client.h"
#include <math.h>

void guac_rdp_process_seamrdp_event(guac_client* client, RDP_EVENT* event) {
	char **fields;
	char *input, *output, *token;
	int nbFields, inputLength, outputLength, i;

	input = (char*)(event->user_data);
	inputLength = strlen(input);

	/* Encode data into Guacamole protocole :
		 <field#1 lenght>.field#1,<field#2 lenght>.field#2,[...];

	   ex : 3.foo,3.bar,7.johnDoe,4.1337;
	*/

	/* Count CSV fields */
	nbFields = 1;
	for(i=0 ; i < inputLength+1 ; ++i)
		if(input[i] == ',') nbFields++; 

	/* Split and copy them into the array */
	i = 0;
	outputLength = 0;
	fields = malloc(sizeof(char*) * nbFields);
	token = strtok(input, ",\n\r");

	while(token != NULL) {
		char *out;
		int inLen = strlen(token);
		int outLen = inLen;

		/* increment length for separators and char count */
		outLen += ((int)(ceil(log10(inLen)))) + 1; /* count digits for the length */
		outLen += 2; /* add space for '.' and ',' */
		out = malloc(outLen+1);

		/* format the string */
		snprintf(out, outLen+1, "%d.%s,", inLen, token);

		/* store it */
		fields[i++] = out;

		/* global size counter */
		outputLength += outLen;

		/* next token */
		token = strtok(NULL, ",\n\r");
	}

	/* Concatenate the fields */
	outputLength += 10; /* length of "7.seamrdp," */
	output = malloc(outputLength+1);
	memset(output, 0, outputLength+1);
	strcat(output, "7.seamrdp,");

	for(i=0 ; i < nbFields ; ++i) {
		strcat(output, fields[i]);
		free(fields[i]);
	}

	/* replace the final ',' by ';' */
	output[strlen(output)-1] = ';';

	guac_socket_write_string(client->socket, output);
	/*printf("%s\n", output);*/
	free(fields);
	free(output);
}
