# Terminal C Editor (termineditor)

Un semplice editor C per terminale. Questa versione è adattata per le console di Windows e utilizza le API Win32 e le sequenze di escape ANSI.

## Features

- **Evidenziazione della Sintassi C**: Colora automaticamente parole chiave, tipi di dati, commenti, stringhe, numeri e direttive del preprocessore.
- **Gestione File Completa**: Apri file esistenti (`Ctrl+O`), salva le modifiche (`Ctrl+S`), e salva nuovi file con un nome personalizzato ("Salva con Nome" automatico).
- **Ricerca nel Testo**: Trova stringhe di testo nel file con una ricerca interattiva (`Ctrl+F`) che permette di navigare tra le occorrenze.
- **Corrispondenza Parentesi**: Trova la parentesi graffa `{}` corrispondente a quella sotto il cursore (`Ctrl+]`).
- **Minimale, Singolo File C**: L'intero editor è contenuto in un unico file sorgente `.c`.
- **Editing Basato su Cursore**: Muoviti nel testo, inserisci caratteri, cancella e crea nuove righe.

## Requirements

- Una console **Windows** che supporti le **sequenze di escape ANSI**:
  - Windows 10 o successivi hanno tipicamente `ENABLE_VIRTUAL_TERMINAL_PROCESSING` abilitato.
  - Su versioni più vecchie di Windows, potrebbe essere necessario un workaround o un driver della console aggiornato.
- Un **compilatore C** in grado di compilare e linkare le API Win32 (es. Visual Studio, MinGW).

## Building

1.  **Installa** un compilatore C su Windows (es. MSVC, MinGW).
2.  **Apri** un terminale o un prompt dei comandi per sviluppatori nella cartella del progetto.
3.  **Compila** il sorgente:

    ```bash
    # Con il compilatore di Visual Studio (cl.exe)
    cl termineditor.c /Fe:termineditor.exe
    ```
    o, se usi MinGW:
    ```bash
    # Con GCC (MinGW)
    gcc termineditor.c -o termineditor.exe
    ```

4.  **Esegui** il file `termineditor.exe` generato in una console Windows che supporti le sequenze ANSI.

## Running

Puoi avviare l'editor con un nome di file opzionale. Per esempio:

```bash
.\termineditor miofile.c
```

Se il file non esiste, verrà creato un nuovo buffer vuoto. Se ometti il nome del file, l'editor partirà con un buffer senza nome che potrai salvare in seguito.

## Keybindings

- **`Ctrl+Q`**  
  Chiude l'editor. Se ci sono modifiche non salvate, ti chiederà di premere `Ctrl+Q` una seconda volta per confermare.

- **`Ctrl+S`**  
  Salva il file corrente. Se il file è nuovo (senza nome), ti chiederà di inserire un nome ("Salva con Nome").

- **`Ctrl+O`**  
  Apre un file. Ti verrà chiesto di inserire il nome del file da aprire. L'operazione verrà annullata se ci sono modifiche non salvate nel file corrente.

- **`Ctrl+F`**  
  Cerca nel testo. Inserisci la parola da cercare e premi Invio. Usa i tasti freccia (Su/Giù) per navigare tra le occorrenze. Premi ESC per annullare.

- **`Ctrl+]`**  
  Trova la parentesi graffa corrispondente a quella su cui si trova il cursore.

- **Tasti Freccia**  
  Spostano il cursore a sinistra, destra, su o giù.

- **Backspace**  
  Cancella il carattere a sinistra del cursore (o unisce le righe se il cursore è all'inizio di una linea).

- **Invio (Enter)**  
  Inserisce una nuova riga alla posizione del cursore.

- **Page Up / Page Down**  
  Sposta il cursore su o giù di una schermata intera.

- **Home / End**  
  Sposta il cursore all'inizio o alla fine della riga corrente.

## Known Limitations / Caveats

1.  **Dimensioni del Terminale**: Se la console viene ridimensionata durante l'uso, l'editor tenta di adattarsi, ma alcune console Windows potrebbero richiedere un riavvio del programma per riconoscere correttamente le nuove dimensioni.
2.  **Windows Datato**: Su versioni di Windows precedenti a Windows 10, l'elaborazione delle sequenze di escape ANSI potrebbe non essere disponibile di default. Potrebbe essere necessario aggiornare o installare una console di terze parti (come ConEmu).
3.  **A Capo Automatico**: Questo editor non supporta l'andata a capo automatica (word wrapping). Le righe lunghe non vengono spezzate e bisogna andare a capo manualmente.

## Contributing

Contributi o suggerimenti sono i benvenuti! Sentiti libero di:

- **Aprire una issue** se trovi dei bug.
- **Inviare una Pull Request (PR)** se hai miglioramenti o nuove funzionalità.
- Fornire feedback su come gestire le differenze di comportamento tra le varie versioni di Windows.

## License

MIT, fai quello che vuoi.