## Prerequisites

This tutorial assumes that you have:

- installed YugabyteDB, created a universe and are able to interact with it using the CQL shell. If not, please follow these steps in the [quick start guide](../../../quick-start/test-cassandra/).
- installed Go version 1.8+

## Install the Go Cassandra driver

To install the driver locally run:

```sh
$ go get github.com/yugabyte/gocql
```

## Writing a HelloWorld CQL application

Create a file `ybcql_hello_world.go` and copy the contents below.

```go
package main;

import (
    "fmt"
    "log"
    "time"

    "github.com/yugabyte/gocql"
)

func main() {
    // Connect to the cluster.
    cluster := gocql.NewCluster("127.0.0.1", "127.0.0.2", "127.0.0.3")

    // Use the same timeout as the Java driver.
    cluster.Timeout = 12 * time.Second

    // Create the session.
    session, _ := cluster.CreateSession()
    defer session.Close()

    // Set up the keyspace and table.
    if err := session.Query("CREATE KEYSPACE IF NOT EXISTS ybdemo").Exec(); err != nil {
        log.Fatal(err)
    }
    fmt.Println("Created keyspace ybdemo")


    if err := session.Query(`DROP TABLE IF EXISTS ybdemo.employee`).Exec(); err != nil {
        log.Fatal(err)
    }
    var createStmt = `CREATE TABLE ybdemo.employee (id int PRIMARY KEY, 
                                                           name varchar, 
                                                           age int, 
                                                           language varchar)`;
    if err := session.Query(createStmt).Exec(); err != nil {
        log.Fatal(err)
    }
    fmt.Println("Created table ybdemo.employee")

    // Insert into the table.
    var insertStmt string = "INSERT INTO ybdemo.employee(id, name, age, language)" + 
        " VALUES (1, 'John', 35, 'Go')";
    if err := session.Query(insertStmt).Exec(); err != nil {
        log.Fatal(err)
    }
    fmt.Printf("Inserted data: %s\n", insertStmt)

    // Read from the table.
    var name string
    var age int
    var language string
    iter := session.Query(`SELECT name, age, language FROM ybdemo.employee WHERE id = 1`).Iter()
    fmt.Printf("Query for id=1 returned: ");
    for iter.Scan(&name, &age, &language) {
        fmt.Printf("Row[%s, %d, %s]\n", name, age, language)
    }

    if err := iter.Close(); err != nil {
        log.Fatal(err)
    }
}
```

## Running the application

To execute the file, run the following command:

```sh
$ go run ybcql_hello_world.go
```

You should see the following as the output.

```
Created keyspace ybdemo
Created table ybdemo.employee
Inserted data: INSERT INTO ybdemo.employee(id, name, age, language) VALUES (1, 'John', 35, 'Go')
Query for id=1 returned: Row[John, 35, Go]
```
