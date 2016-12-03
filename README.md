# regen_link_script
---

+ get run-time funcion stack

    - use GCC flag

        > `-finstrument-functions`

        ```
        void __attribute__((__no_instrument_function__))
        __cyg_profile_func_enter(void *this_func, void *call_site)
        {
            ...
        }

        void __attribute__((__no_instrument_function__))
        __cyg_profile_func_exit(void *this_func, void *call_site)
        {
            ...
        }

        *this_func   : function address
        *call_site : caller address

        ```

+ get GCC RTL file

    - enable GCC flag

        > `-fdump-rtl-expand`

    - collect `*.expand` in Linux

        ```
        $ cat $(find ./ -name *.expand) > all.expand
        ```

+ get GCC map file

    - enable link option

        > `-Wl,-Map=output.map`

+ get symbol table from `ELF`

    ```
    $ nm -C xxx.elf > xxx.symbol
    ```


