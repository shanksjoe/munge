#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

int
main (int argc, char *argv[])
{
    struct group   *gr_ptr;
    struct passwd  *pw_ptr;
    char          **gr_mem_ptr;
    gid_t           gid;
    char           *user;
    uid_t           uid;
    gid_t           gid_tmp;

    setgrent ();

    while (1) {
        errno = 0; 
        gr_ptr = getgrent ();

        if (gr_ptr == NULL) {
            if ((errno == 0) || (errno == ENOENT))
                break;
            if (errno == EINTR)
                continue;
            fprintf (stderr, "*ERROR* cannot query group info: %s",
                    strerror (errno));
            exit (EXIT_FAILURE);
        }
        gid = gr_ptr->gr_gid;
        printf ("  group [%s] (gid=%u)\n",
                (gr_ptr->gr_name ? gr_ptr->gr_name : "*NULL*"), gid);

        gr_mem_ptr = gr_ptr->gr_mem;
        while ((gr_mem_ptr != NULL) && (*gr_mem_ptr != NULL)) {

            user = *gr_mem_ptr;
            pw_ptr = getpwnam (user);

            if (pw_ptr == NULL) {
                printf ("*ERROR* cannot query passwd info for [%s]: %s\n",
                        (user ? user : "*NULL*"), strerror (errno));
            }
            else {
                uid = pw_ptr->pw_uid;
                printf ("    user [%s] (uid=%u)\n",
                        (user ? user : "*NULL*"), uid);

                gid_tmp = gr_ptr->gr_gid;
                if (gid_tmp != gid) {
                    printf ("*ERROR* gid %u changed to gid %u\n",
                            gid, gid_tmp);
                }
            }
            gr_mem_ptr++;
        }
    }
    endgrent ();
    exit (EXIT_SUCCESS);
}
