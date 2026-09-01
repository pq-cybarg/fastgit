#include "fastgit/network.h"
#include <stdlib.h>
#include <string.h>

#if defined(FASTGIT_HAVE_OQS) && FASTGIT_HAVE_OQS
#include <oqs/oqs.h>
#endif

fastgit_error_t fastgit_pqc_key_generate(fastgit_pqc_algo_t algo, fastgit_pqc_key_t** out) {
    if (!out) return FASTGIT_EINVAL;
    (void)algo;

#if defined(FASTGIT_HAVE_OQS) && FASTGIT_HAVE_OQS
    const char* oqs_algo = NULL;
    switch (algo) {
        case FASTGIT_PQC_ALGO_ML_DSA_44: oqs_algo = "ML-DSA-44"; break;
        case FASTGIT_PQC_ALGO_ML_DSA_65: oqs_algo = "ML-DSA-65"; break;
        case FASTGIT_PQC_ALGO_ML_DSA_87: oqs_algo = "ML-DSA-87"; break;
        case FASTGIT_PQC_ALGO_SLH_DSA_SHA2_128F: oqs_algo = "SPHINCS+-SHA2-128f-simple"; break;
        case FASTGIT_PQC_ALGO_SLH_DSA_SHA2_128S: oqs_algo = "SPHINCS+-SHA2-128s-simple"; break;
        case FASTGIT_PQC_ALGO_SLH_DSA_SHA2_192F: oqs_algo = "SPHINCS+-SHA2-192f-simple"; break;
        case FASTGIT_PQC_ALGO_SLH_DSA_SHA2_256F: oqs_algo = "SPHINCS+-SHA2-256f-simple"; break;
        case FASTGIT_PQC_ALGO_FALCON_512: oqs_algo = "Falcon-512"; break;
        case FASTGIT_PQC_ALGO_FALCON_1024: oqs_algo = "Falcon-1024"; break;
        default: return FASTGIT_EUNSUPPORTED;
    }

    OQS_SIG* sig = OQS_SIG_new(oqs_algo);
    if (!sig) return FASTGIT_EUNSUPPORTED;

    fastgit_pqc_key_t* key = calloc(1, sizeof(fastgit_pqc_key_t));
    if (!key) {
        OQS_SIG_free(sig);
        return FASTGIT_ENOMEM;
    }

    key->algo = algo;
    key->public_key = malloc(sig->length_public_key);
    key->private_key = malloc(sig->length_secret_key);
    key->public_key_len = sig->length_public_key;
    key->private_key_len = sig->length_secret_key;

    if (!key->public_key || !key->private_key) {
        fastgit_pqc_key_free(key);
        OQS_SIG_free(sig);
        return FASTGIT_ENOMEM;
    }

    OQS_STATUS ret = OQS_SIG_keypair(sig, key->public_key, key->private_key);
    OQS_SIG_free(sig);

    if (ret != OQS_SUCCESS) {
        fastgit_pqc_key_free(key);
        return FASTGIT_ERROR;
    }

    *out = key;
    return FASTGIT_OK;
#else
    return FASTGIT_EUNSUPPORTED;
#endif
}

void fastgit_pqc_key_free(fastgit_pqc_key_t* key) {
    if (!key) return;
    free(key->public_key);
    free(key->private_key);
    free(key);
}

fastgit_error_t fastgit_pqc_sign(const fastgit_pqc_key_t* key, const void* data, size_t len, uint8_t** sig, size_t* sig_len) {
    if (!key || !data || !sig || !sig_len) return FASTGIT_EINVAL;
    (void)len;
    (void)sig;
    (void)sig_len;

#if defined(FASTGIT_HAVE_OQS) && FASTGIT_HAVE_OQS
    const char* oqs_algo = NULL;
    switch (key->algo) {
        case FASTGIT_PQC_ALGO_ML_DSA_44: oqs_algo = "ML-DSA-44"; break;
        case FASTGIT_PQC_ALGO_ML_DSA_65: oqs_algo = "ML-DSA-65"; break;
        case FASTGIT_PQC_ALGO_ML_DSA_87: oqs_algo = "ML-DSA-87"; break;
        case FASTGIT_PQC_ALGO_SLH_DSA_SHA2_128F: oqs_algo = "SPHINCS+-SHA2-128f-simple"; break;
        case FASTGIT_PQC_ALGO_SLH_DSA_SHA2_128S: oqs_algo = "SPHINCS+-SHA2-128s-simple"; break;
        case FASTGIT_PQC_ALGO_SLH_DSA_SHA2_192F: oqs_algo = "SPHINCS+-SHA2-192f-simple"; break;
        case FASTGIT_PQC_ALGO_SLH_DSA_SHA2_256F: oqs_algo = "SPHINCS+-SHA2-256f-simple"; break;
        case FASTGIT_PQC_ALGO_FALCON_512: oqs_algo = "Falcon-512"; break;
        case FASTGIT_PQC_ALGO_FALCON_1024: oqs_algo = "Falcon-1024"; break;
        default: return FASTGIT_EUNSUPPORTED;
    }

    OQS_SIG* sig_obj = OQS_SIG_new(oqs_algo);
    if (!sig_obj) return FASTGIT_EUNSUPPORTED;

    *sig = malloc(sig_obj->length_signature);
    if (!*sig) {
        OQS_SIG_free(sig_obj);
        return FASTGIT_ENOMEM;
    }

    OQS_STATUS ret = OQS_SIG_sign(sig_obj, *sig, sig_len, (const uint8_t*)data, len, key->private_key);
    OQS_SIG_free(sig_obj);

    if (ret != OQS_SUCCESS) {
        free(*sig);
        *sig = NULL;
        return FASTGIT_ERROR;
    }

    return FASTGIT_OK;
#else
    return FASTGIT_EUNSUPPORTED;
#endif
}

fastgit_error_t fastgit_pqc_verify(const fastgit_pqc_key_t* key, const void* data, size_t len, const uint8_t* sig, size_t sig_len) {
    if (!key || !data || !sig) return FASTGIT_EINVAL;
    (void)len;
    (void)sig;
    (void)sig_len;

#if defined(FASTGIT_HAVE_OQS) && FASTGIT_HAVE_OQS
    const char* oqs_algo = NULL;
    switch (key->algo) {
        case FASTGIT_PQC_ALGO_ML_DSA_44: oqs_algo = "ML-DSA-44"; break;
        case FASTGIT_PQC_ALGO_ML_DSA_65: oqs_algo = "ML-DSA-65"; break;
        case FASTGIT_PQC_ALGO_ML_DSA_87: oqs_algo = "ML-DSA-87"; break;
        case FASTGIT_PQC_ALGO_SLH_DSA_SHA2_128F: oqs_algo = "SPHINCS+-SHA2-128f-simple"; break;
        case FASTGIT_PQC_ALGO_SLH_DSA_SHA2_128S: oqs_algo = "SPHINCS+-SHA2-128s-simple"; break;
        case FASTGIT_PQC_ALGO_SLH_DSA_SHA2_192F: oqs_algo = "SPHINCS+-SHA2-192f-simple"; break;
        case FASTGIT_PQC_ALGO_SLH_DSA_SHA2_256F: oqs_algo = "SPHINCS+-SHA2-256f-simple"; break;
        case FASTGIT_PQC_ALGO_FALCON_512: oqs_algo = "Falcon-512"; break;
        case FASTGIT_PQC_ALGO_FALCON_1024: oqs_algo = "Falcon-1024"; break;
        default: return FASTGIT_EUNSUPPORTED;
    }

    OQS_SIG* sig_obj = OQS_SIG_new(oqs_algo);
    if (!sig_obj) return FASTGIT_EUNSUPPORTED;

    OQS_STATUS ret = OQS_SIG_verify(sig_obj, (const uint8_t*)data, len, sig, sig_len, key->public_key);
    OQS_SIG_free(sig_obj);

    return ret == OQS_SUCCESS ? FASTGIT_OK : FASTGIT_ERROR;
#else
    return FASTGIT_EUNSUPPORTED;
#endif
}
