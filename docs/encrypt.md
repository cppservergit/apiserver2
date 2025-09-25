# Environment variables encryption for APIServer2

## 1. Prerequisites

It is assumed that the procedure will be run on Linux Ubuntu 24.04 using openssl V3.x.

### 1.1. General Overview

APIServer2 uses OpenSSL and asymmetric encryption to support encrypted environment variables, as defined in the script file named `run.sh`, located in the binary installation folder (usually `/home/ubuntu/`).

Whenever APIServer2 reads a string environment variable, like `JWT_SECRET` or `LOGINDB`, if the value ends with `.enc` it will assume that it must decrypt a file located in the same folder where the name of the file is the value of the variable. It will try to use a private key named `private.pem` for decryption. The .enc and private.pem files must be located in the same directory as the APIServer2 executable.

The `.enc` files and the `private.pem` key should be produced by a Security Admin and distributed to the person in charge of the installation and configuration of APIServer2.

## 2. Public Key Security

The public key should be kept in a safe place, separate from the APIServer2 installation. APIServer2 only requires the private key to decrypt the files.

## 3. Generate RSA Key Pair
```
openssl genrsa -out private.pem 2048
openssl rsa -in private.pem -outform PEM -pubout -out public.pem
```

## 4. Encrypt the SHA26 Secret for the JSON Web Token (JWT)

First, we need to save the plain-text secret in a text file without a CRLF at the end.
```
echo -n 'B@as!ca123*' > secret.txt
```
**Note:** Use a secret that is difficult to guess; it is even better if you let OpenSSL generate one for you.

**Example:**
```
echo -n $(openssl rand -base64 24) > secret.txt
```

## 5. Encrypt the Secret File

Encrypt the secret using the public key.
```
openssl pkeyutl -encrypt -pubin -inkey public.pem -in secret.txt -out secret.enc
```
 **Note:** For security purposes, all the steps mentioned above should be executed on a separate machine not used to run APIServer2.

## 6. Apply Encrypted Values to an APIServer2 Installation

**Note:** It is assumed that you are familiar with the installation procedure of a binary distribution of APIServer2. 

Copy `private.pem` and `secret.enc` into the APIServer2 directory.

Edit the `run.sh` script and change the value of `JWT_SECRET` like this:
```
export JWT_SECRET="secret.enc"
```

Save the file and restart the APIServer++ service.

## 7. Encrypt an ODBC Connection String

Save the connection string to a text file without a CRLF at the end.
```
echo -n 'Driver=FreeTDS;SERVER=demodb.mshome.net;PORT=1433;DATABASE=testdb;UID=sa;PWD=Basica2024;APP=apiserver;Encryption=off;ClientCharset=UTF-8' > logindb.txt
```

Encrypt the text file.
```
openssl pkeyutl -encrypt -pubin -inkey public.pem -in logindb.txt -out logindb.enc
```

Apply the new encrypted value. Copy the `.enc` file to the installation directory of APIServer2, then edit the `run.sh` script to change the value of the corresponding environment variable, like this:
```
export LOGINDB="logindb.enc"
```
Save and restart the service.
