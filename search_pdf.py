import PyPDF2
import re
import sys

pdf_path = r'D:\Project\NECCS\Program\NECCS\stm32n6资料\stm32n6资料\官方n6评估板\mb1939-n6570-c02-schematic.pdf'

try:
    with open(pdf_path, 'rb') as f:
        reader = PyPDF2.PdfReader(f)
        for i, page in enumerate(reader.pages):
            text = page.extract_text()
            if text:
                if 'USB2' in text or 'USB1' in text or 'OTG_HS2' in text or 'OTG_HS1' in text or 'WiFi' in text.lower():
                    print(f'--- Page {i+1} ---')
                    lines = text.split('\n')
                    for j, line in enumerate(lines):
                        if re.search(r'USB[12]|OTG_HS[12]|wifi', line, re.IGNORECASE):
                            start = max(0, j-2)
                            end = min(len(lines), j+3)
                            print('\n'.join(lines[start:end]))
                            print('...')
except Exception as e:
    print(f'Error: {e}')
