int cap = 1; /* if section headers should be capitalized */

size_t sec = 1;     /* default section number */
char *title = NULL; /* default title, NULL if set to filename */
char *date  = NULL; /* default date man page generated, NULL if set to current YYYY-MM-DD */
char *ver   = NULL; /* default optional version of programming being documented, NULL if none */
char *mid   = NULL; /* default optional text to be displayed in the middle */

int namesec = 0;
char *synsec = NULL; /* optional text for synopsis section to be inserted */
int descsec = 0;     /* insert section heading for description */
