#include "build_identity.h"

#include <stdio.h>

#include "esp_app_desc.h"
#include "keyvalet_build_identity.h"

const char *keyvalet_build_identity(void)
{
    static char identity[KEYVALET_BUILD_ID_MAX];
    if (identity[0] == '\0') {
        char elf_sha256[65];
        const esp_app_desc_t *app = esp_app_get_description();
        for (size_t index = 0; index < sizeof(app->app_elf_sha256); ++index) {
            snprintf(elf_sha256 + index * 2U, 3, "%02x",
                     app->app_elf_sha256[index]);
        }
        snprintf(identity, sizeof(identity), "%s-%s",
                 KEYVALET_BUILD_PREFIX, elf_sha256);
    }
    return identity;
}

const char *keyvalet_build_variant(void)
{
    return KEYVALET_BUILD_VARIANT;
}
