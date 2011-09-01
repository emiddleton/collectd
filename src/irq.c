/**
 * collectd - src/irq.c
 * Copyright (C) 2007  Peter Holik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Peter Holik <peter at holik.at>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_ignorelist.h"

#if !KERNEL_LINUX
# error "No applicable input method."
#endif

#define BUFSIZE 128

/*
 * (Module-)Global variables
 */
static const char *config_keys[] =
{
	"Irq",
	"IgnoreSelected"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static ignorelist_t *ignorelist = NULL;

static int irq_config (const char *key, const char *value)
{
	if (ignorelist == NULL)
		ignorelist = ignorelist_create (/* invert = */ 1);
	if (ignorelist == NULL)
		return (1);

	if (strcasecmp ("Irq", key) == 0)
	{
		ignorelist_add (ignorelist, value);
	}
	else if (strcasecmp ("IgnoreSelected", key) == 0)
	{
		int invert = 1;
		if (IS_TRUE (value))
			invert = 0;
		ignorelist_set_invert (ignorelist, invert);
	}
	else
	{
		return (-1);
	}
	return (0);
}

static void irq_submit (const char *irq, derive_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;
	int status;

	if (ignorelist_match (ignorelist, irq) != 0)
		return;

	values[0].derive = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "irq", sizeof (vl.plugin));
	sstrncpy (vl.type, "irq", sizeof (vl.type));
	sstrncpy (vl.type_instance, irq, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* void irq_submit */

int irq_parse_value (const char *value, value_t *ret_value)
{
	char *endptr = NULL;
	ret_value->derive = (derive_t) strtoll (value, &endptr, 0);
	if (value == endptr) {
		return -1;
	}
	else if ((NULL != endptr) && ('\0' != *endptr))
		WARNING ("parse_value: Ignoring trailing garbage after number: %s.",
		endptr);
	return 0;
} /* int irq_parse_value */

static int irq_read (void)
{
	FILE *fh;
	char buffer[1024];

	fh = fopen ("/proc/interrupts", "r");
	if (fh == NULL)
	{
		char errbuf[1024];
		ERROR ("irq plugin: fopen (/proc/interrupts): %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	while (fgets (buffer, BUFSIZE, fh) != NULL)
	{
		char irq_name[64];
		int irq_name_len;
		derive_t irq_value;
		char *endptr;
		int i;

		char *fields[64];
		int fields_num;

		fields_num = strsplit (buffer, fields, 64);
		if (fields_num < 2)
			continue;

		errno = 0;    /* To distinguish success/failure after call */
		irq_name_len = ssnprintf (irq_name, sizeof (irq_name), "%s", fields[0]);
		endptr = &irq_name[irq_name_len-1];

		if ((endptr == fields[0]) || (errno != 0) || (*endptr != ':'))
			continue;

		/* remove : */
		*endptr = 0;

		irq_value = 0;
		for (i = 1; i < fields_num; i++)
		{
			/* Per-CPU value */
			value_t v;
			int status;

			status = irq_parse_value (fields[i], &v);
			if (status != 0)
				break;

			irq_value += v.derive;
		} /* for (i) */

		irq_submit (irq_name, irq_value);
	}

	fclose (fh);

	return (0);
} /* int irq_read */

void module_register (void)
{
	plugin_register_config ("irq", irq_config,
			config_keys, config_keys_num);
	plugin_register_read ("irq", irq_read);
} /* void module_register */

#undef BUFSIZE
