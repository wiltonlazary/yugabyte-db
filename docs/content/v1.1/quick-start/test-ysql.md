---
title: 5. Test YSQL API
linkTitle: 5. Test YSQL API
description: Test Yugabyte SQL (YSQL) API
beta: /faq/product/#what-is-the-definition-of-the-beta-feature-tag
menu:
  v1.1:
    parent: quick-start
    weight: 145
type: page
---

Follow the instructions below to test YugabyteDB's PostgreSQL-compatible [YSQL](../../api/ysql/) API.

[**psql**](https://www.postgresql.org/docs/9.3/static/app-psql.html) is a command line shell for interacting with PostgreSQL. For ease of use, YugabyteDB ships with a version of psql in its bin directory.


## 1. Create a new cluster

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#macos" class="nav-link active" id="macos-tab" data-toggle="tab" role="tab" aria-controls="macos" aria-selected="true">
      <i class="fab fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>
  <li>
    <a href="#linux" class="nav-link" id="linux-tab" data-toggle="tab" role="tab" aria-controls="linux" aria-selected="false">
      <i class="fab fa-linux" aria-hidden="true"></i>
      Linux
    </a>
  </li>
  <li>
    <a href="#docker" class="nav-link" id="docker-tab" data-toggle="tab" role="tab" aria-controls="docker" aria-selected="false">
      <i class="fab fa-docker" aria-hidden="true"></i>
      Docker
    </a>
  </li>
  <li >
    <a href="#kubernetes" class="nav-link" id="kubernetes-tab" data-toggle="tab" role="tab" aria-controls="kubernetes" aria-selected="false">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="macos" class="tab-pane fade show active" role="tabpanel" aria-labelledby="macos-tab">
    {{% includeMarkdown "binary/test-postgresql.md" /%}}
  </div>
  <div id="linux" class="tab-pane fade" role="tabpanel" aria-labelledby="linux-tab">
    {{% includeMarkdown "binary/test-postgresql.md" /%}}
  </div>
  <div id="docker" class="tab-pane fade" role="tabpanel" aria-labelledby="docker-tab">
    {{% includeMarkdown "docker/test-postgresql.md" /%}}
  </div>
  <div id="kubernetes" class="tab-pane fade" role="tabpanel" aria-labelledby="kubernetes-tab">
    {{% includeMarkdown "kubernetes/test-postgresql.md" /%}}
  </div>
</div>


## 2. Create a table

Create a database called 'sample'.

```sql
postgres=> CREATE DATABASE sample;
```

Connect to the database we just created.

```sql
postgres=> \c sample
```

```
You are now connected to database "sample" as user "postgres".
sample=>
```


Create a table named 'stock_market' which can store stock prices at various timestamps for different stock ticker symbols.

```sql
sample=> CREATE TABLE stock_market (
  stock_symbol text,
  ts text,
  current_price float,
  PRIMARY KEY (stock_symbol, ts)
);
```




## 3. Insert data

Let us insert some data for a few stock symbols into our newly created 'stock_market' table. You can copy-paste these values directly into your psql shell.

```sql
INSERT INTO stock_market (stock_symbol,ts,current_price) VALUES ('AAPL','2017-10-26 09:00:00',157.41);
INSERT INTO stock_market (stock_symbol,ts,current_price) VALUES ('AAPL','2017-10-26 10:00:00',157);
INSERT INTO stock_market (stock_symbol,ts,current_price) VALUES ('FB','2017-10-26 09:00:00',170.63);
INSERT INTO stock_market (stock_symbol,ts,current_price) VALUES ('FB','2017-10-26 10:00:00',170.1);
INSERT INTO stock_market (stock_symbol,ts,current_price) VALUES ('GOOG','2017-10-26 09:00:00',972.56);
INSERT INTO stock_market (stock_symbol,ts,current_price) VALUES ('GOOG','2017-10-26 10:00:00',971.91);
```

## 4. Query the table

Query all the values we have inserted into the table.

```sql
sample=> SELECT * FROM stock_market ORDER BY stock_symbol ASC, ts DESC;
```

```
 stock_symbol |         ts          | current_price
--------------+---------------------+---------------
 AAPL         | 2017-10-26 10:00:00 |           157
 AAPL         | 2017-10-26 09:00:00 |        157.41
 FB           | 2017-10-26 10:00:00 |         170.1
 FB           | 2017-10-26 09:00:00 |        170.63
 GOOG         | 2017-10-26 10:00:00 |        971.91
 GOOG         | 2017-10-26 09:00:00 |        972.56
(6 rows)
```
