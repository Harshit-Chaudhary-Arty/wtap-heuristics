#ifndef PARSER_H
#define PARSER_H

#include "wtap.h"

/* Parse all instances from the input file. Returns number read, or -1 on error. */
int parse_instances(const char *filename, WTAPInstance *instances, int max_instances);

#endif /* PARSER_H */
