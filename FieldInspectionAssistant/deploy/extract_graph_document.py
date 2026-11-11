"""Read a user-selected text PDF without changing the inference environment."""
from pathlib import Path
import sys
from pypdf import PdfReader

def extract(path):
    source=Path(path)
    if not source.is_file() or source.stat().st_size>5*1024*1024:
        raise ValueError('PDF exceeds size limit')
    reader=PdfReader(source)
    if reader.is_encrypted or len(reader.pages)>200:
        raise ValueError('Encrypted PDF or more than 200 pages')
    chunks=[];size=0
    for page in reader.pages:
        text=page.extract_text() or ''
        size+=len(text)+1
        if size>60000:raise ValueError('More than 60000 characters; split the document')
        chunks.append(text)
    text='\n'.join(chunks).strip()
    if len(text)<4:raise ValueError('PDF has no extractable text')
    return text

if __name__=='__main__':
    try:sys.stdout.buffer.write(extract(sys.argv[1]).encode('utf-8'))
    except Exception as error:
        print(str(error),file=sys.stderr);sys.exit(1)
