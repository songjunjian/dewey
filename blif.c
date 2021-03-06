#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include "blif.h"

#define CHUNK_SIZE 128

// #define BLIF_DEBUG

/*
 * gets the name of the net associated with net_t i
 */
char *get_net_name(struct blif *blif, net_t i)
{
	if (i < blif->n_nets)
		return blif->net_names[i];

	return NULL;
}

/*
 * gets the id of the net based on the name, or 0 if not found
 */
net_t get_net_id(struct blif *blif, char *name)
{
	net_t i;

	for (i = 1; i < blif->n_nets; i++)
		if (strcmp(blif->net_names[i], name) == 0)
			return i;

	return 0;
}

/*
 * adds this blif to the list of nets, unless it already exists.
 * either way, it returns the net id.
 */
net_t add_net(struct blif *blif, char *name)
{
	net_t id;

	id = get_net_id(blif, name);

	if (!id) {
		id = blif->n_nets;
		blif->n_nets++;
		blif->net_names = realloc(blif->net_names, sizeof(char *) * blif->n_nets);
		blif->net_names[id] = strdup(name);
		printf("[blif] net %d -> %s\n", id, blif->net_names[id]);
	}

	return id;
}

struct blif_line {
	char *line;
	size_t len;
};

static struct blif_line read_blif_line(FILE *f)
{
	size_t cur_buf_size, len, pos;
	size_t bytes_read;
	char *line;

	if (feof(f))
		return (struct blif_line){NULL, 0};

	cur_buf_size = 0;
	len = 0;
	pos = 0;

	line = malloc(sizeof(char) * cur_buf_size);

	/* read all of the characters in CHUNK_SIZE pieces until a newline is found */
	for (;;) {
		/* read in more bytes of the file */
		cur_buf_size += CHUNK_SIZE;
		line = realloc(line, sizeof(char) * cur_buf_size);

		/* read in the line */
		bytes_read = fread(line + len, sizeof(char), CHUNK_SIZE, f);

		/* end of file, or error */
		if (!bytes_read && ferror(f)) {
			printf("[blif] error reading file: %s\n", strerror(errno));
			free(line);
			return (struct blif_line){NULL, 0};
		}

		len += bytes_read;

		for (; pos < len; pos++) {
			if (line[pos] == '\n' && !(pos > 0 && line[pos - 1] == '\\')) {
				/* rewind for the next line */
				fseek(f, (pos - len + 1) * sizeof(char) , SEEK_CUR);
				line[pos] = '\0';
				len = pos;
				break;
			}
		}

		if (line[pos] == '\0')
			break;

		if (!bytes_read && feof(f))
			break;
	}

	/* eliminate any comment lines, backslash/newlines */
	for (pos = 0; pos < len; pos++) {
		if (line[pos] == '\0') {
			break;
		} else if (line[pos] == '#') {
			/* null-terminate at comments */
			line[pos] = '\0';
			len = pos;
			break;
		} else if (pos > 0 && line[pos] == '\n' && line[pos - 1] == '\\') {
			/* replace backslash/newline sequence with spaces */
			line[pos - 1] = ' ';
			line[pos] = ' ';
		}
	}

	return (struct blif_line){line, cur_buf_size};
}

static void read_inputs(struct blif *blif, char *line)
{
	char *tmp;
	net_t net;

	while ((tmp = strsep(&line, " \t")) != NULL) {
		net = add_net(blif, tmp);
		if (blif->n_inputs++) {
			blif->inputs = realloc(blif->inputs, sizeof(net_t) * blif->n_inputs);
		} else {
			blif->inputs = malloc(sizeof(net_t));
		}
		blif->inputs[blif->n_inputs - 1] = net;
	}

#ifdef BLIF_DEBUG
	for (int i = 0; i < blif->n_inputs; i++)
		printf("[blif] input %d = %d (%s)\n", i, blif->inputs[i], get_net_name(blif, blif->inputs[i]));
#endif
}

static void read_outputs(struct blif *blif, char *line)
{
	char *tmp;
	net_t net;

	while ((tmp = strsep(&line, " \t")) != NULL) {
		net = add_net(blif, tmp);
		if (blif->n_outputs++) {
			blif->outputs = realloc(blif->outputs, sizeof(net_t) * blif->n_outputs);
		} else {
			blif->outputs = malloc(sizeof(net_t));
		}
		blif->outputs[blif->n_outputs - 1] = net;
	}

#ifdef BLIF_DEBUG
	for (int i = 0; i < blif->n_outputs; i++)
		printf("[blif] output %d = %d (%s)\n", i, blif->outputs[i], get_net_name(blif, blif->outputs[i]));
#endif
}

static void read_subckt(struct blif *blif, char *line)
{
	char *tmp;
	struct blif_cell *cell;
	struct pin *pin;
	unsigned int pin_id;

	/* subcircuit name */
	tmp = strsep(&line, " \t");
	if (!tmp) {
		printf("[blif] could not read subckt name\n");
		return;
	}

	cell = malloc(sizeof(struct blif_cell));
	cell->name = strdup(tmp);
	cell->pins = NULL;
	cell->n_pins = 0;

#ifdef BLIF_DEBUG
	printf("[blif] subckt %d name = %s\n", blif->n_cells, cell->name);
#endif

	/* subcircuit pin=net mappings */
	while (line) {
		pin = malloc(sizeof(struct pin));

		/* copy pin name */
		tmp = strsep(&line, "=");
		pin->name = strdup(tmp);

		/* get net id */
		tmp = strsep(&line, " \t");
		pin->net = add_net(blif, strdup(tmp));

#ifdef BLIF_DEBUG
		printf("[blif] pin: %s = net %d (%s)\n", pin->name, pin->net, get_net_name(blif, pin->net));
#endif

		/* add the pin */
		pin_id = cell->n_pins++;
		cell->pins = realloc(cell->pins, sizeof(struct pin *) * cell->n_pins);
		cell->pins[pin_id] = pin;
	}
	
	/* add the cell */
	if (blif->n_cells++) {
		blif->cells = realloc(blif->cells, sizeof(struct blif_cell *) * blif->n_cells);
	} else {
		blif->cells = malloc(sizeof(struct blif_cell *));
	}
	blif->cells[blif->n_cells - 1] = cell;
}

struct blif *read_blif(FILE *f)
{
	struct blif *blif;
	struct blif_line bl;
	size_t line_size;
	char *line, *tofree;
	char *tmp;

	blif = malloc(sizeof(struct blif));
	blif->n_inputs = 0;
	blif->n_outputs = 0;
	blif->n_cells = 0;

	/* initialize the list of net names, and let the first one be null */
	blif->model = NULL;
	blif->n_nets = 1;
	blif->net_names = malloc(sizeof(char *) * blif->n_nets);
	blif->net_names[0] = NULL;

	/* read the blif file */
	bl = read_blif_line(f);
	tofree = line = bl.line;
	line_size = bl.len;

	while (line) {
		if (strncmp(line, ".model", 6) == 0 && !blif->model) {
			/* .model
			 * subsequent declarations of .model are ignored
			 */
			for (tmp = line + 6; *tmp == ' ' || *tmp == '\t'; tmp++);
			blif->model = strdup(tmp);
			printf("[blif] model = %s\n", blif->model);

		} else if (strncmp(line, ".inputs", 7) == 0) {
			/* .inputs */
			for (tmp = line + 7; *tmp == ' ' || *tmp == '\t'; tmp++);
			read_inputs(blif, tmp);

		} else if (strncmp(line, ".outputs", 8) == 0) {
			/* .inputs */
			for (tmp = line + 8; *tmp == ' ' || *tmp == '\t'; tmp++);
			read_outputs(blif, tmp);

		} else if (strncmp(line, ".subckt", 7) == 0) {
			/* .subckt */
			for (tmp = line + 7; *tmp == ' ' || *tmp == '\t'; tmp++);
			read_subckt(blif, tmp);

		} else if (strnlen(line, line_size - (line - tofree)) > 0) {
#ifdef BLIF_DEBUG
			printf("[blif line] %s\n", line);
#endif
		}

		free(tofree);
		bl = read_blif_line(f);
		tofree = line = bl.line;
		line_size = bl.len;
	}

	return blif;
}

void free_blif(struct blif *blif)
{
	int i, j;

	/* free model name */
	free(blif->model);

	/* net names; the first is NULL */
	for (i = 1; i < blif->n_nets; i++)
		free(blif->net_names[i]);

	free(blif->net_names);

	/* inputs and outputs */
	free(blif->inputs);
	free(blif->outputs);

	/* cells */
	for (i = 0; i < blif->n_cells; i++) {
		/* cell pins */
		for (j = 0; j < blif->cells[i]->n_pins; j++)
			free(blif->cells[i]->pins[j]->name);

		free(blif->cells[i]->pins);
		free(blif->cells[i]->name);

		free(blif->cells[i]);
	}

	free(blif);
}
