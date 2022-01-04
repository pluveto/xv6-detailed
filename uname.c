#include "types.h"
#include "stat.h"
#include "user.h"

int
main (void)
{
  char _uname[8];
  int ret =   uname(_uname);
  if (ret != 0)
    {
      write (2, "failed to get uname", 20);
      goto EXIT;
    }
  write (1, _uname, strlen (_uname));

EXIT:
  write (1, "\n", 2);
  exit ();
}
