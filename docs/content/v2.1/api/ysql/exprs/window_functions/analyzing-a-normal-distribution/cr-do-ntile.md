---
title: cr_do_ntile.sql
linkTitle: cr_do_ntile.sql
headerTitle: cr_do_ntile.sql
description: Create the function that creates the histogram output.
block_indexing: true
menu:
  v2.1:
    identifier: cr-do-ntile
    parent: analyzing-a-normal-distribution
    weight: 110
isTocNested: true
showAsideToc: true
---
Save this script as `cr_do_ntile.sql`.
```postgresql
create or replace procedure do_ntile(no_of_buckets in int)
  language sql
as $body$
  insert into results(method, bucket, n, min_s, max_s)
  with
    ntiles as (
      select
        score,
        (ntile(no_of_buckets) over w) as bucket
      from t4_view
      window w as (order by score))

  select
    'ntile',
    bucket,
    count(*),
    min(score),
    max(score)
  from ntiles
  group by bucket;
$body$;
```
