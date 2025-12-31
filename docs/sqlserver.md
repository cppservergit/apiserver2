# QuickStart SQL Server with docker on Ubuntu 24.04

Let's assume that you have a brand-new Ubuntu 24.04 virtual machine, if it was created with multipass you will use the `ubuntu` user and its `/home/ubuntu` directory.
Please log in to this VM and make sure you are in your home directory.

When using multipass you can create the minimal VM (2 cores, 4GB RAM, 12GB disk space) with this command:
```
multipass launch -n demodb -c 2 -m 4G -d 10G
```
Log into the VM:
```
multipass shell demodb
```

**Reference**: 
<a href="https://multipass.run/install" target="_blank" rel="noopener noreferrer">
Install Multipass on your system
</a>

## Step 1: Install tree
This software will let you see the full directory structure and verify that SQL Server filled it with its data/log files.
```
sudo apt update && sudo apt install tree
```

## Step 2: Install latest Docker engine
```
curl -fsSL https://get.docker.com -o get-docker.sh && sh get-docker.sh && sudo sh get-docker.sh
```
When the installation process ends you can check the results with:
```
sudo docker version
```
This is an express way to install the docker engine on Ubuntu and it can be updated with the rest of the system using the regular `sudo apt update && sudo apt upgrade` command.

## Step 3: Pull SQL Server 2019 image for docker
This may take a few minutes, it is a large image.
```
sudo docker pull mcr.microsoft.com/mssql/server:2019-latest
```

## Step 4: Create a directory structure for SQL Server data files
```
mkdir sql && mkdir sql/data && mkdir sql/log && mkdir sql/secrets
```
Add permissions so the docker container can write into this directory
```
sudo chown -R 10001:0 sql && sudo chmod -R 770 sql
```

## Step 5: Run SQL Server container
You may change the value of MSSQL_SA_PASSWORD, do not touch the rest of the command, it is a single line.
```
sudo docker run -d --restart unless-stopped --name mssql --network host -v ./sql/data:/var/opt/mssql/data -v ./sql/log:/var/opt/mssql/log -v ./sql/secrets:/var/opt/mssql/secrets -e "ACCEPT_EULA=Y" -e "MSSQL_PID=Developer" -e "MSSQL_SA_PASSWORD=Basica2024" mcr.microsoft.com/mssql/server:2019-latest
```
Please wait a few seconds, then proceed to the next step.

## Step 6: Verify that SQL Server created the basic data files.
```
sudo tree sql
```

You should see something like this:
```
sql
├── data
│   ├── Entropy.bin
│   ├── master.mdf
│   ├── mastlog.ldf
│   ├── model.mdf
│   ├── model_msdbdata.mdf
│   ├── model_msdblog.ldf
│   ├── model_replicatedmaster.ldf
│   ├── model_replicatedmaster.mdf
│   ├── modellog.ldf
│   ├── msdbdata.mdf
│   ├── msdblog.ldf
│   ├── tempdb.mdf
│   ├── tempdb2.ndf
│   ├── tempdb3.ndf
│   ├── tempdb4.ndf
│   ├── templog.ldf
├── log
│   ├── HkEngineEventFile_0_133550319560340000.xel
│   ├── errorlog
│   ├── errorlog.1
│   ├── log.trc
│   ├── sqlagentstartup.log
│   └── system_health_0_133550319567040000.xel
└── secrets
    └── machine-key
```

## Step 7: Test database connectivity:
```
sudo docker exec -it mssql /opt/mssql-tools18/bin/sqlcmd -No -S localhost -U SA -P 'Basica2024' -Q "SELECT @@VERSION;"
```
Expected output (versions may vary):
```
Microsoft SQL Server 2019 (RTM-CU32-GDR) (KB5068404) - 15.0.4455.2 (X64)
        Oct  7 2025 21:10:15
        Copyright (C) 2019 Microsoft Corporation
        Developer Edition (64-bit) on Linux (Ubuntu 20.04.6 LTS) <X64>
```

## Step 8: Download API-Server demo database backups
```
curl https://cppserver.com/res/demodb.bak -O && \
curl https://cppserver.com/res/testdb.bak -O
```

## Step 9: Move the backups to the data directory of the container
```
sudo mv *.bak sql/data
```

## Step 10: Restore the backups

### demodb
```
sudo docker exec -it mssql /opt/mssql-tools18/bin/sqlcmd -No -S localhost \
   -U SA -P 'Basica2024' \
   -Q 'RESTORE DATABASE demodb FROM DISK="/var/opt/mssql/data/demodb.bak" WITH REPLACE, RECOVERY;'
```
Expected output:
```
Processed 736 pages for database 'demodb', file 'demo' on file 1.
Processed 2 pages for database 'demodb', file 'demo_log' on file 1.
RESTORE DATABASE successfully processed 738 pages in 0.040 seconds (144.042 MB/sec).
```
### testdb
This is the security database that serves as an example of integration with a custom SQL-based security mechanism (users, roles, etc), of particular importance is the stored procedure `cpp_dblogin`, regardless of the security database structure, API-Server++ login module expects an SP with this name, parameters, and other conventions; the SP is fully documented inside.
```
sudo docker exec -it mssql /opt/mssql-tools18/bin/sqlcmd -No -S localhost \
   -U SA -P 'Basica2024' \
   -Q 'RESTORE DATABASE testdb FROM DISK="/var/opt/mssql/data/testdb.bak" WITH REPLACE, RECOVERY;'
```
Expected out
```
Processed 496 pages for database 'testdb', file 'testdb' on file 1.
Processed 2 pages for database 'testdb', file 'testdb_log' on file 1.
RESTORE DATABASE successfully processed 498 pages in 0.041 seconds (94.798 MB/sec).
```

That's it, your SQL Server docker container is ready to use, when using Multipass on Windows 10/11, other VMs should be able to connect to it using the DNS name `demodb.mshome.net`, it is visible on the multipass subnet only, inside your Windows host.

## Step 11: List databases
```
sudo docker exec -it mssql /opt/mssql-tools18/bin/sqlcmd -No -S localhost -U SA -P 'Basica2024' -Q "SELECT name FROM sys.databases;"
```
Expected output:
```
master
tempdb
model
msdb
demodb
testdb
```
## Notes

### ODBC connection strings
APIServer2 uses FreeTDS ODBC Driver, a fast and solid driver that works with SQL Server and Sybase.
```
Driver=FreeTDS;SERVER=demodb.mshome.net;PORT=1433;DATABASE=testdb;UID=sa;PWD=Basica2024;APP=APIServer2;Encryption=off;ClientCharset=UTF-8
Driver=FreeTDS;SERVER=demodb.mshome.net;PORT=1433;DATABASE=demodb;UID=sa;PWD=Basica2024;APP=APIServer2;Encryption=off;ClientCharset=UTF-8
```

Using encryption is possible with this ODBC driver, we disable it by default for development, troubleshooting encryption configuration between the client and the SQL Server is beyond the scope of this guide, please refer to the driver's documentation.

* [Free TDS ODBC connection properties](https://www.freetds.org/userguide/freetdsconf.html) Look for table 3.3 at the end of the document.

You may have noticed that we include the `APP` attribute on the connection string, this is useful to monitor APIServer2 connections on the server.

### Executing clean backups in SQL Server 2019
Assuming you are using the same base directory as in the steps above, you must remove the previous BAK file using:
```
sudo rm sql/data/testdb.bak
```
If you don't remove the BAK file, the command below will fail with a `permission denied` error.

It's necessary to perform a backup with the overwrite option `with INIT`, otherwise, when you restore the database you may see old data, and your backup file keeps growing:
```
sudo docker exec -it mssql /opt/mssql-tools18/bin/sqlcmd -No -S localhost \
   -U SA -P 'Basica2024' \
   -Q 'backup database testdb to disk="/var/opt/mssql/data/testdb.bak" with INIT';
```
It is also desirable to shrink your database before executing the backup using the [DBCC SHRINKDATABASE](https://learn.microsoft.com/en-us/sql/t-sql/database-console-commands/dbcc-shrinkdatabase-transact-sql?view=sql-server-ver16) SQL command.

### Using the SQL command line console inside the docker container
It might be the case that your docker container is not visible to remote GUI clients, this could be the case if you installed it in a multipass VM and then you want to connect from a remote machine, the VM won't be visible by default when using Multipass unless you configured a bridge for that purpose. In this case, you can enter a terminal session inside your docker container, right from the VM where docker is installed of course:
```
sudo docker exec -it mssql /opt/mssql-tools18/bin/sqlcmd -No -S localhost -U SA -P 'Basica2024'
```
After executing this command, you are inside sqlcmd running in your docker container  `mssql`, now you can execute SQL commands like this:
```
use master
go
sp_databases
go
```

### Removing the docker container
```
sudo docker stop mssql && \
sudo docker rm mssql && \
sudo docker system prune
```

### Removing the Multipass VM
From a Windows CMD shell:
Stop and delete VM:
```
multipass delete demodb
```
Clean disk space:
```
multipass purge
```
