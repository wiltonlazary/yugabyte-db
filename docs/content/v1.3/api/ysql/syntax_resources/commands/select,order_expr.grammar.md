```
select ::= [ WITH [ RECURSIVE ] { with_query [ , ... ] } ]  SELECT 
           [ ALL | DISTINCT [ ON { ( expression [ , ... ] ) } ] ] 
           [ * | { expression [ [ AS ] name ] } [ , ... ] ]  
           [ FROM { from_item [ , ... ] } ] [ WHERE condition ]  
           [ GROUP BY { grouping_element [ , ... ] } ] 
           [ HAVING { condition [ , ... ] } ]  
           [ { UNION | INTERSECT | EXCEPT } [ ALL | DISTINCT ] select ] 
           [ ORDER BY { order_expr [ , ... ] } ]  
           [ LIMIT [ integer | ALL ] ] 
           [ OFFSET integer [ ROW | ROWS ] ]

order_expr ::= expression [ ASC | DESC | USING operator ] 
               [ NULLS { FIRST | LAST } ]
```
