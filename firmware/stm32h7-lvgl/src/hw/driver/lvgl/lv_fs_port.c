#include "lv_fs_port.h"
#include "fs.h"
#include "lfs.h"


/* FatFs 드라이버(S:)는 lv_init() 안에서 자동 등록되므로 할 일이 없다.
 * littlefs 드라이버(F:)만 lfs 인스턴스를 물려주면 된다.
 */
void lv_fs_port_init(void)
{
#if LV_USE_FS_LITTLEFS
  lv_littlefs_set_handler((lfs_t *)fsGetHandle());
#endif
}
