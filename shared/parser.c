#include "parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Read the next floating-point number from a text line.
 * Skips brackets, commas, and spaces. Advances *pos as it goes.
 */
static int next_double_from_line(const char *text, int *pos, double *value) {
    while (text[*pos] != '\0' &&
           !isdigit((unsigned char)text[*pos]) &&
           text[*pos] != '.' && text[*pos] != '-') {
        (*pos)++;
    }

    if (text[*pos] == '\0') {
        return 0;
    }

    char *end = NULL;
    *value = strtod(&text[*pos], &end);
    if (end == &text[*pos]) {
        return 0;
    }

    *pos = (int)(end - text);
    return 1;
}

/* Parse: V_j Values: [48.0, 99.0, ...] */
static int parse_target_values(const char *line, WTAPInstance *inst) {
    int pos = 0;
    int count = 0;

    while (count < inst->num_targets &&
           next_double_from_line(line, &pos, &inst->target_values[count])) {
        count++;
    }

    return count == inst->num_targets;
}

/*
 * Read the next double from a file, skipping brackets, commas, etc.
 * Returns 0 when no more numbers are found (e.g. end of matrix).
 */
static int read_next_double(FILE *fp, double *value) {
    int ch;
    char token[64];
    int len = 0;

    while ((ch = fgetc(fp)) != EOF) {
        if (isdigit(ch) || ch == '.' || ch == '-') {
            break;
        }
    }

    if (ch == EOF) {
        return 0;
    }

    token[len++] = (char)ch;
    while (len < (int)sizeof(token) - 1) {
        ch = fgetc(fp);
        if (ch == EOF) {
            break;
        }
        if (!isdigit(ch) && ch != '.' && ch != '-' && ch != '+' &&
            ch != 'e' && ch != 'E') {
            ungetc(ch, fp);
            break;
        }
        token[len++] = (char)ch;
    }

    token[len] = '\0';
    *value = strtod(token, NULL);
    return 1;
}

/* Read num_weapons x num_targets probability matrix from the open file */
static int read_probability_matrix(FILE *fp, WTAPInstance *inst) {
    int needed = inst->num_weapons * inst->num_targets;
    int count = 0;

    while (count < needed) {
        double value;
        if (!read_next_double(fp, &value)) {
            return 0;
        }

        int w = count / inst->num_targets;
        int t = count % inst->num_targets;
        inst->prob[w][t] = value;
        count++;
    }

    return 1;
}

int parse_instances(const char *filename, WTAPInstance *instances, int max_instances) {
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        fprintf(stderr, "Error: cannot open file '%s'\n", filename);
        return -1;
    }

    int count = 0;
    char line[8192];

    while (count < max_instances && fgets(line, sizeof(line), fp) != NULL) {
        if (strncmp(line, "INSTANCE", 8) != 0) {
            continue;
        }

        WTAPInstance *inst = &instances[count];

        if (sscanf(line, "INSTANCE %d: n=%d, m=%d",
                   &inst->instance_id, &inst->num_weapons, &inst->num_targets) != 3) {
            fprintf(stderr, "Warning: could not parse header: %s", line);
            continue;
        }

        if (fgets(line, sizeof(line), fp) == NULL) {
            break;
        }
        if (!parse_target_values(line, inst)) {
            fprintf(stderr, "Warning: bad target values for instance %d\n",
                    inst->instance_id);
            continue;
        }

        /* Skip "P_ij Matrix:" line */
        if (fgets(line, sizeof(line), fp) == NULL) {
            break;
        }

        if (!read_probability_matrix(fp, inst)) {
            fprintf(stderr, "Warning: incomplete matrix for instance %d\n",
                    inst->instance_id);
            continue;
        }

        count++;
    }

    fclose(fp);
    return count;
}
