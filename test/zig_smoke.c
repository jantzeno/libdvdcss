/* Public API smoke test for the Zig build. */
#include <stddef.h>

#include <dvdcss/dvdcss.h>

int main(void)
{
    if (DVDCSS_VERSION != DVDCSS_VERSION_CODE(1, 6, 0))
        return 1;

    dvdcss_t dvdcss = dvdcss_open("__libdvdcss_missing_device__");
    if (dvdcss != NULL)
    {
        dvdcss_close(dvdcss);
        return 2;
    }

    return 0;
}
