/* Actual security/enrollment and mbedTLS P-256. Fake clock/NVS/MAC only.
 * Fresh forked firmware instances share durable storage, never volatile keys. */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../main/security.c"

typedef struct {
    bool present;
    trust_blob_t trust;
    keyvalet_replay_window_t replay;
    unsigned trust_writes, replay_writes;
    int failure;
} storage_t;
static storage_t *durable;
static trust_blob_t pending;
static int64_t microseconds=1000000, utc=1800000000;
static bool valid_time=true;
static uint32_t random_state=12345;
static mbedtls_ecp_keypair owner, other;
static char owner_hex[131], other_hex[131], challenge_hex[65], signature_hex[129];
static uint32_t remaining;

static int random_bytes(void *unused,unsigned char *bytes,size_t length)
{(void)unused;while(length--){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;*bytes++=(uint8_t)random_state;}return 0;}
void esp_fill_random(void *buffer,size_t length) {(void)random_bytes(NULL,buffer,length);}
int64_t esp_timer_get_time(void) {return microseconds;}
bool keyvalet_time_get_utc(int64_t *now) {*now=utc;return valid_time;}
esp_err_t esp_read_mac(uint8_t *bytes,int type)
{(void)type;const uint8_t mac[]={0,1,2,3,4,5};memcpy(bytes,mac,6);return ESP_OK;}
const char *keyvalet_network_device_id(void) {return "keyvalet-030405";}
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{SemaphoreHandle_t mutex=malloc(sizeof(*mutex));assert(mutex && !pthread_mutex_init(mutex,NULL));return mutex;}
int xSemaphoreTake(SemaphoreHandle_t mutex,unsigned timeout)
{(void)timeout;assert(!pthread_mutex_lock(mutex));return 1;}
int xSemaphoreGive(SemaphoreHandle_t mutex) {assert(!pthread_mutex_unlock(mutex));return 1;}
uint32_t esp_crc32_le(uint32_t initial,const uint8_t *data,uint32_t length)
{uint32_t crc=~initial;while(length--){crc^=*data++;for(unsigned i=0;i<8;++i)crc=(crc>>1)^(0xedb88320U&(0U-(crc&1U)));}return ~crc;}
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *handle)
{assert(!strcmp(name,"keyvalet") && mode==NVS_READWRITE);*handle=1;return ESP_OK;}
esp_err_t nvs_get_blob(nvs_handle_t handle,const char *key,void *data,size_t *length)
{assert(handle==1 && !strcmp(key,"trust_v1"));if(!durable->present)return ESP_ERR_NVS_NOT_FOUND;assert(*length>=104);memcpy(data,&durable->trust,104);*length=104;return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t handle,const char *key,const void *data,size_t length)
{assert(handle==1 && !strcmp(key,"trust_v1") && length==104);++durable->trust_writes;if(durable->failure==1)return ESP_FAIL;memcpy(&pending,data,104);return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t handle)
{assert(handle==1);durable->trust=pending;durable->present=true;return durable->failure==2?ESP_FAIL:ESP_OK;}
esp_err_t keyvalet_replay_store_init(bool enrolled,keyvalet_replay_window_t *window)
{(void)enrolled;*window=durable->replay;return ESP_OK;}
esp_err_t keyvalet_replay_store_append(const uint8_t operation[16],const uint8_t nonce[16],int64_t now)
{++durable->replay_writes;assert(keyvalet_replay_window_add(&durable->replay,operation,nonce,now));return ESP_OK;}

static void key_init(mbedtls_ecp_keypair *key,char hex[131])
{
    mbedtls_ecp_keypair_init(key);
    assert(!mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1,key,random_bytes,NULL));
    uint8_t point[65];size_t length;
    assert(!mbedtls_ecp_point_write_binary(&key->grp,&key->Q,MBEDTLS_ECP_PF_UNCOMPRESSED,&length,point,65) && length==65);
    encode_hex(point,length,hex);
}
static void sign_text(mbedtls_ecp_keypair *key,const char *message,char hex[129])
{
    uint8_t digest[32],signature[64];mbedtls_mpi r,s;mbedtls_mpi_init(&r);mbedtls_mpi_init(&s);
    assert(!mbedtls_sha256((const unsigned char *)message,strlen(message),digest,0));
    assert(!mbedtls_ecdsa_sign(&key->grp,&r,&s,&key->d,digest,32,random_bytes,NULL));
    assert(!mbedtls_mpi_write_binary(&r,signature,32) && !mbedtls_mpi_write_binary(&s,signature+32,32));
    encode_hex(signature,64,hex);mbedtls_mpi_free(&r);mbedtls_mpi_free(&s);
}
static esp_err_t stage(const char *key)
{char fingerprint[65];uint32_t seconds;return keyvalet_security_stage_enrollment_key(key,fingerprint,&seconds);}
static void begin(void)
{
    assert(stage(owner_hex)==ESP_OK);
    assert(keyvalet_security_open_pairing_window()==ESP_OK);
    assert(keyvalet_security_begin_enrollment(challenge_hex,&remaining)==ESP_OK && remaining<=300 && remaining>0);
}
static void proof(mbedtls_ecp_keypair *key,const char *public_key)
{char message[512];snprintf(message,sizeof(message),"KEYVALET-ENROLL-V1\nkeyvalet-030405\n%s\n%s\nENROLL_FIRST\n%" PRId64 "\n",challenge_hex,public_key,utc+60);sign_text(key,message,signature_hex);}
static esp_err_t complete(const char *public_key)
{return keyvalet_security_complete_enrollment(challenge_hex,public_key,utc+60,signature_hex);}
static void signed_commands(void)
{
    for(unsigned lock=0;lock<2;++lock){
        ++utc; /* Existing replay policy permits only strictly increasing accept UTC. */
        char nonce[65],operation[64];memset(nonce,'1'+lock,64);nonce[64]=0;
        snprintf(operation,sizeof(operation),"m10-security-check-%u",lock);
        keyvalet_authorization_t auth={.operation_id=operation,.nonce_hex=nonce,.issued_at=utc,.expires_at=utc+60,.signature_hex=signature_hex};
        keyvalet_unlock_authorization_payload_t unlock={.device_id=keyvalet_network_device_id(),.lock=lock,
            .operation_id=auth.operation_id,.nonce_hex=nonce,.issued_at=utc,.expires_at=utc+60};
        char message[768];assert(keyvalet_unlock_v2_canonical(message,sizeof(message),&unlock)>0);
        sign_text(&other,message,signature_hex);
        assert(keyvalet_security_authorize_and_consume(KEYVALET_AUTH_UNLOCK,&auth,NULL,&unlock,NULL)!=ESP_OK);
        sign_text(&owner,message,signature_hex);
        assert(keyvalet_security_authorize_and_consume(KEYVALET_AUTH_UNLOCK,&auth,NULL,&unlock,NULL)==ESP_OK);
        assert(keyvalet_security_authorize_and_consume(KEYVALET_AUTH_UNLOCK,&auth,NULL,&unlock,NULL)!=ESP_OK);
    }
    assert(durable->replay_writes==2);
}
static void successful(void)
{
    assert(keyvalet_security_init()==ESP_OK);begin();proof(&owner,owner_hex);
    assert(complete(owner_hex)==ESP_OK && keyvalet_security_is_enrolled());
    assert(durable->present && durable->trust_writes==1 && !s_enrollment_key_deadline_us && !s_challenge_active);
    assert(stage(other_hex)!=ESP_OK && keyvalet_security_open_pairing_window()!=ESP_OK);
    signed_commands();
}
static void persisted(void)
{
    assert(keyvalet_security_init()==ESP_OK && keyvalet_security_is_enrolled());
    assert(stage(other_hex)!=ESP_OK && keyvalet_security_begin_enrollment(challenge_hex,&remaining)!=ESP_OK);
    assert(complete(other_hex)!=ESP_OK && durable->trust_writes==1);
    uint8_t key[65];assert(decode_hex_exact(owner_hex,key,65) && !memcmp(key,s_trust.operational_public_key,65));
}
static void corrupt_trust(void)
{assert(keyvalet_security_init()==ESP_ERR_INVALID_CRC);assert(!keyvalet_security_is_enrolled() && stage(owner_hex)!=ESP_OK);}
static void no_physical_authorization(void)
{
    assert(keyvalet_security_init()==ESP_OK);
    assert(keyvalet_security_open_pairing_window()==ESP_OK);
    assert(keyvalet_security_begin_enrollment(challenge_hex,&remaining)!=ESP_OK); /* No USB key. */
    keyvalet_security_close_pairing_window();assert(stage(owner_hex)==ESP_OK);
    assert(!keyvalet_security_pairing_active() && s_enrollment_key_deadline_us);
    assert(keyvalet_security_begin_enrollment(challenge_hex,&remaining)!=ESP_OK); /* No button. */
    assert(keyvalet_security_open_pairing_window()==ESP_OK);
    assert(keyvalet_security_begin_enrollment(challenge_hex,&remaining)==ESP_OK);
    proof(&other,other_hex);assert(complete(other_hex)!=ESP_OK);
    assert(!s_enrollment_key_deadline_us && !s_challenge_active && !durable->present);
    assert(keyvalet_security_begin_enrollment(challenge_hex,&remaining)!=ESP_OK);
    begin();proof(&owner,owner_hex);assert(complete(owner_hex)==ESP_OK);
}
static void competition_and_expiry(void)
{
    assert(keyvalet_security_init()==ESP_OK);begin();
    int64_t deadline=s_enrollment_key_deadline_us;
    microseconds+=1000000;assert(stage(owner_hex)==ESP_OK && s_enrollment_key_deadline_us==deadline);
    assert(stage(other_hex)!=ESP_OK && s_enrollment_key_deadline_us==deadline);
    char competing[65];assert(keyvalet_security_begin_enrollment(competing,&remaining)!=ESP_OK);
    char retained[65];encode_hex(s_challenge,32,retained);assert(!strcmp(challenge_hex,retained));
    proof(&owner,owner_hex);microseconds=deadline;
    assert(complete(owner_hex)!=ESP_OK && !s_enrollment_key_deadline_us && !s_challenge_active);
    begin();keyvalet_security_close_pairing_window();assert(!s_enrollment_key_deadline_us);
    assert(stage(owner_hex)==ESP_OK);microseconds=s_enrollment_key_deadline_us;
    (void)keyvalet_security_pairing_active();assert(!s_enrollment_key_deadline_us);
    keyvalet_security_close_pairing_window();assert(keyvalet_security_open_pairing_window()==ESP_OK);
    microseconds+=599000000;assert(stage(owner_hex)==ESP_OK);
    assert(keyvalet_security_begin_enrollment(challenge_hex,&remaining)==ESP_OK && remaining==1);
    microseconds+=1000000;assert(!keyvalet_security_pairing_active() && !s_enrollment_key_deadline_us);
}
static void invalid_proofs(void)
{
    assert(keyvalet_security_init()==ESP_OK);
    char invalid[131];memset(invalid,'0',130);invalid[130]=0;invalid[1]='4';
    assert(stage(invalid)!=ESP_OK && !s_enrollment_key_deadline_us); /* Off curve. */
    strcpy(invalid,owner_hex);invalid[2]='A';assert(stage(invalid)!=ESP_OK);
    assert(stage("04")!=ESP_OK);
    begin();proof(&other,owner_hex);assert(complete(owner_hex)!=ESP_OK && !s_enrollment_key_deadline_us);
    begin();proof(&owner,owner_hex);challenge_hex[0]=challenge_hex[0]=='a'?'b':'a';assert(complete(owner_hex)!=ESP_OK);
    begin();proof(&owner,owner_hex);valid_time=false;assert(complete(owner_hex)!=ESP_OK);valid_time=true;
    begin();proof(&owner,owner_hex);assert(keyvalet_security_complete_enrollment(challenge_hex,owner_hex,utc-1,signature_hex)!=ESP_OK);
    assert(!durable->present && !durable->trust_writes);
}
static void staged_only(void) {assert(keyvalet_security_init()==ESP_OK);begin();assert(!durable->present);}
static void no_volatile_state(void)
{assert(keyvalet_security_init()==ESP_OK);assert(!s_enrollment_key_deadline_us && !s_challenge_active);assert(keyvalet_security_open_pairing_window()==ESP_OK);assert(keyvalet_security_begin_enrollment(challenge_hex,&remaining)!=ESP_OK);}
static void write_failure(void)
{
    assert(keyvalet_security_init()==ESP_OK);begin();proof(&owner,owner_hex);
    assert(complete(owner_hex)!=ESP_OK && !s_ready && !s_enrollment_key_deadline_us);
    durable->failure=0;assert(stage(other_hex)!=ESP_OK && complete(other_hex)!=ESP_OK && durable->trust_writes==1);
}
static void failure_reboot(void)
{assert(keyvalet_security_init()==ESP_OK);assert(keyvalet_security_is_enrolled()==durable->present);assert((stage(other_hex)==ESP_OK)==!durable->present);}
static void *competing_completion(void *result) {*(esp_err_t *)result=complete(owner_hex);return NULL;}
static void concurrent_completion(void)
{
    assert(keyvalet_security_init()==ESP_OK);begin();proof(&owner,owner_hex);
    pthread_t first,second;esp_err_t a,b;
    assert(!pthread_create(&first,NULL,competing_completion,&a) && !pthread_create(&second,NULL,competing_completion,&b));
    assert(!pthread_join(first,NULL) && !pthread_join(second,NULL));
    assert((a==ESP_OK)!=(b==ESP_OK));assert(durable->trust_writes==1 && durable->present);
}
static void run(void (*phase)(void))
{pid_t child=fork();assert(child>=0);if(!child){phase();_exit(0);}int status;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);}
int main(void)
{
    key_init(&owner,owner_hex);key_init(&other,other_hex);
    durable=mmap(NULL,sizeof(*durable),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);assert(durable!=MAP_FAILED);
    run(successful);run(persisted);
    memset(durable->trust.operational_public_key,0,65);durable->trust.operational_public_key[0]=4;
    durable->trust.crc32=blob_crc(&durable->trust,offsetof(trust_blob_t,crc32));run(corrupt_trust);
    memset(durable,0,sizeof(*durable));run(no_physical_authorization);
    memset(durable,0,sizeof(*durable));run(competition_and_expiry);
    memset(durable,0,sizeof(*durable));run(invalid_proofs);
    memset(durable,0,sizeof(*durable));run(staged_only);run(no_volatile_state);
    for(int mode=1;mode<=2;++mode){memset(durable,0,sizeof(*durable));durable->failure=mode;run(write_failure);run(failure_reboot);}
    memset(durable,0,sizeof(*durable));run(concurrent_completion);
    puts("Enrollment passed: actual security + mbedTLS, USB key binding/button/proof, competing/substituted keys, expiry/reboot, ambiguous NVS, persisted authority, signed commands/replay");
}
