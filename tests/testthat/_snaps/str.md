# rds_str() summarises without building

    Code
      rds_str(f)
    Output
      List of 9
       $ id: int [1:3]
       $ fit:List of 2
        ..$ coef: num [1:2]
        ..$ m: int [1:4] (matrix)
       $ when: 'Date' num [1:1]
       $ df:'data.frame':	3 obs. of  2 variables:
        ..$ x: int [1:3]
        ..$ f: Factor w/ 2 levels "a","b": [1:3]
       $ fn: function
       $ e: <environment: R_GlobalEnv>
       $ s: symbol sym
       $ u: NULL
       $ seq: int <compact_intseq>

---

    Code
      rds_str(f, max_level = 1)
    Output
      List of 9
       $ id: int [1:3]
       $ fit:List of 2
       $ when: 'Date' num [1:1]
       $ df:'data.frame':	3 obs. of  2 variables:
       $ fn: function
       $ e: <environment: R_GlobalEnv>
       $ s: symbol sym
       $ u: NULL
       $ seq: int <compact_intseq>

---

    Code
      rds_str(f, max_level = 0)
    Output
      List of 9

# rds_str() lists the objects of an .rda

    Code
      rds_str(test_path("fixtures", "multi-xdr-v3-gzip.rda"))
    Output
      .rda with 4 objects:
       $ integer: int [1:4]
       $ character: chr [1:5]
       $ data_frame:'data.frame':	3 obs. of  2 variables:
        ..$ n: int [1:3]
        ..$ s: chr [1:3]
       $ named_list:List of 2
        ..$ x: num [1:1]
        ..$ y:List of 1

# rds_str() names every kind of item

    Code
      stats::setNames(out, objs)
    Output
      $null
      [1] " NULL"
      
      $logical
      [1] " logi [1:3]"
      
      $integer
      [1] " int [1:4]"
      
      $double
      [1] " num [1:7]"
      
      $complex
      [1] " cplx [1:3]"
      
      $character
      [1] " chr [1:5]"
      
      $raw
      [1] " raw [1:3]"
      
      $factor
      [1] " Factor w/ 2 levels \"lo\",\"hi\": [1:3]"
      
      $matrix
      [1] " int [1:6] (matrix)"
      
      $pairlist
      [1] "Dotted pair list"
      
      $symbol
      [1] " symbol sym"
      
      $language
      [1] " language"
      
      $closure
      [1] " function"
      
      $bytecode
      [1] " function"
      
      $environment
      [1] " <environment>"
      
      $formula
      [1] " 'formula' language"
      
      $s4
      [1] " Formal class 'ZurdsFixture'"
      
      $external_pointer
      [1] " <externalptr>"
      
      $compact_seq
      [1] " int <compact_intseq>"
      
      $deferred_string
      [1] " chr <deferred_string>"
      
      $builtin
      [1] " primitive"
      
      $special
      [1] " primitive"
      
      $namespace_closure
      [1] " function"
      
      $package_env
      [1] " <environment: package>"
      
      $promise
      [1] " <environment>"
      

