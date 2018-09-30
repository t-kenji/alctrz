/** @file       caps.cpp
 *  @brief      Capabilities Unit Test.
 *
 *  @author     t-kenji <protect.2501@gmail.com>
 *  @date       2018-09-30 create new.
 *  @copyright  Copyright © 2018 t-kenji
 *
 *  This code is licensed under the MIT License.
 */
#include <cstdio>
#include <cstdlib>
#include <cstdbool>
#include <cstdint>
#include <cerrno>
#include <cstring>
#include <inttypes.h>
#include <sys/mount.h>

#include "catch2/catch.hpp"

SCENARIO("test for CAP_SYS_ADMIN", "[caps]") {
    GIVEN("set CAP_SYS_ADMIN ambient capabirity") {
        WHEN("mount") {
            THEN("success mount") {
                REQUIRE(mount("none", "/mnt", "tmpfs", 0, NULL) == 0);
            }
            REQUIRE(umount("/mnt") == 0);
        }
    }
}
