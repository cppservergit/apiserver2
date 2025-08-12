#include "pkeyutil.hpp"

#include <vector>
#include <fstream>
#include <iterator>
#include <memory>
#include <format>
#include <chrono>

#include <openssl/pem.h>
#include <openssl/evp.h>

// Custom deleters for OpenSSL resources
namespace {
    struct EVP_PKEY_Deleter {
        void operator()(EVP_PKEY* pkey) const { EVP_PKEY_free(pkey); }
    };

    struct EVP_PKEY_CTX_Deleter {
        void operator()(EVP_PKEY_CTX* ctx) const { EVP_PKEY_CTX_free(ctx); }
    };

    struct FILE_Deleter {
        void operator()(FILE* f) const { 
            if (f) fclose(f); 
        }
    };

    using unique_EVP_PKEY = std::unique_ptr<EVP_PKEY, EVP_PKEY_Deleter>;
    using unique_EVP_PKEY_CTX = std::unique_ptr<EVP_PKEY_CTX, EVP_PKEY_CTX_Deleter>;
    using unique_FILE = std::unique_ptr<FILE, FILE_Deleter>;
}

DecryptionResult decrypt(std::string_view filename) noexcept {
    // Use std::ifstream for safe and automatic file handling
    std::ifstream encrypted_file(filename.data(), std::ios::binary);
    if (!encrypted_file) {
        return {false, "Error: Could not open encrypted file."};
    }

    // Read the entire file into a vector to avoid buffer overflows
    std::vector<unsigned char> encrypted_data(
        (std::istreambuf_iterator<char>(encrypted_file)),
        std::istreambuf_iterator<char>()
    );

    unique_FILE private_key_file(fopen("private.pem", "r"));
    if (!private_key_file) {
        return {false, "Error: Could not open private key file."};
    }

    unique_EVP_PKEY evp_private_key(PEM_read_PrivateKey(private_key_file.get(), nullptr, nullptr, nullptr));
    if (!evp_private_key) {
        return {false, "Error: Failed to read private key."};
    }

    unique_EVP_PKEY_CTX dec_ctx(EVP_PKEY_CTX_new(evp_private_key.get(), nullptr));
    if (!dec_ctx) {
        return {false, "Error: Failed to create EVP_PKEY_CTX."};
    }

    if (EVP_PKEY_decrypt_init(dec_ctx.get()) <= 0) {
        return {false, "Error: Failed to initialize decryption."};
    }
    if (EVP_PKEY_CTX_set_rsa_padding(dec_ctx.get(), RSA_PKCS1_PADDING) <= 0) {
        return {false, "Error: Failed to set RSA padding."};
    }

    size_t decrypted_len;
    // Determine the required buffer size for the decrypted data
    if (EVP_PKEY_decrypt(dec_ctx.get(), nullptr, &decrypted_len, encrypted_data.data(), encrypted_data.size()) <= 0) {
        return {false, "Error: Failed to determine decrypted data length."};
    }

    std::vector<unsigned char> decrypted(decrypted_len);
    if (EVP_PKEY_decrypt(dec_ctx.get(), decrypted.data(), &decrypted_len, encrypted_data.data(), encrypted_data.size()) <= 0) {
        return {false, "Error: Decryption failed."};
    }
    
    // Trim potential null terminators or padding
    decrypted.resize(decrypted_len);

    return {true, std::string(reinterpret_cast<const char*>(decrypted.data()), decrypted.size())};
}
