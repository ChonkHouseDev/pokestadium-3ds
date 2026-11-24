# Comprobar que el C hace lo mismo que el juego original

El decomp tiene funciones marcadas `NON_MATCHING` (el C no produce el mismo código que el original) y unas pocas que
siguen solo en ensamblador. Algunas de esas versiones en C no hacen lo mismo que el original, y en el port eso se
ve como fondos negros, listas vacías, voces mudas o decisiones raras de la consola. Estas herramientas las encuentran
corriendo las dos versiones en un emulador MIPS (unicorn) con la misma memoria inventada y comparando qué llaman, qué
escriben y qué regresan.

Necesitan el árbol del decomp ya compilado (`DECOMP`, con `build/` y el compilador IDO en `tools/ido`), `unicorn`,
`pyelftools` y `mips-linux-gnu-objdump`. Los archivos de trabajo van a `PS_VERIF` (por omisión `/tmp/ps_verif`).

- `nm.py > $PS_VERIF/nm.txt`: compila con IDO cada archivo con funciones `NON_MATCHING` y hace la primera criba.
- `dx.py [funciones]`: ejecución diferencial de cada función de `nm.txt` (original contra el C del decomp).
- `alt.py <archivo.c> <funciones> [+cabecera.h]`: lo mismo, pero con la versión del **port** de ese archivo (con sus
  arreglos, `-DPORT`). Sirve para confirmar que un arreglo dejó la función igual al original.
- `nd.py <archivo.c> <funcion>`: diff del ensamblador original contra el C compilado, sin nombres de registros.
- `dx2.py`: compara un tramo de código original (del ELF) contra su traducción de `../m2c.py` (el decodificador de voces).

Avisos falsos conocidos: funciones con variables `static` propias (cambia el orden del `.bss`), macros que cargan
fragmentos (`FRAGMENT_LOAD`) y corridas que llegan al límite de instrucciones.
