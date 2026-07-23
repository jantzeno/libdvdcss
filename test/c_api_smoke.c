#include <dvdcss/dvdcpxm.h>
#include <dvdcss/dvdcss.h>

#include <stddef.h>
#include <string.h>

int main(void) {
  dvdcss_stream_cb callbacks = {0};

  if (dvdcss_open_stream((void *)1, &callbacks) != NULL)
    return 1;

  if (dvdcss_error(NULL) == NULL ||
      strcmp(dvdcss_error(NULL), "invalid libdvdcss handle") != 0)
    return 2;

  if (dvdcss_seek(NULL, 0, DVDCSS_NOFLAGS) != -1)
    return 3;
  if (dvdcss_read(NULL, NULL, 0, DVDCSS_NOFLAGS) != -1)
    return 4;
  if (dvdcss_readv(NULL, NULL, 0, DVDCSS_NOFLAGS) != -1)
    return 5;
  if (dvdcss_is_scrambled(NULL) != -1)
    return 6;
  if (dvdcss_get_encryption_type(NULL) != -1)
    return 7;
  if (dvdcss_close(NULL) != -1)
    return 8;

  if (dvdcpxm_init(NULL, NULL) != -1)
    return 9;
  if (dvdcpxm_seek(NULL, 0, DVDCSS_NOFLAGS) != -1)
    return 10;
  if (dvdcpxm_read(NULL, NULL, 0, DVDCSS_NOFLAGS) != -1)
    return 11;
  if (dvdcpxm_readv(NULL, NULL, 0, DVDCSS_NOFLAGS) != -1)
    return 12;
  if (dvdcpxm_close(NULL) != -1)
    return 13;

  return 0;
}
