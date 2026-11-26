import os
import json
import base64
from pathlib import Path

def ler_arquivo(caminho_arquivo):
    """
    Lê o conteúdo de um arquivo, tratando arquivos de texto e binários.
    """
    try:
        # Tenta ler como texto primeiro
        with open(caminho_arquivo, 'r', encoding='utf-8') as f:
            conteudo = f.read()
            tipo = 'texto'
    except (UnicodeDecodeError, UnicodeError):
        # Se falhar, lê como binário e converte para base64
        try:
            with open(caminho_arquivo, 'rb') as f:
                conteudo_binario = f.read()
                conteudo = base64.b64encode(conteudo_binario).decode('utf-8')
                tipo = 'binario_base64'
        except Exception as e:
            conteudo = f"Erro ao ler arquivo: {str(e)}"
            tipo = 'erro'
    except Exception as e:
        conteudo = f"Erro ao ler arquivo: {str(e)}"
        tipo = 'erro'
    
    return conteudo, tipo

def percorrer_diretorio(diretorio_base, extensoes_filtro=None, excluir_diretorios=None):
    """
    Percorre um diretório e seus subdiretórios, lendo todos os arquivos.
    
    Args:
        diretorio_base (str): Caminho do diretório a ser percorrido
        extensoes_filtro (list, optional): Lista de extensões para filtrar (ex: ['.txt', '.py'])
        excluir_diretorios (list, optional): Lista de nomes de diretórios para excluir
    
    Returns:
        dict: Dicionário com a estrutura dos arquivos e conteúdos
    """
    
    if extensoes_filtro is None:
        extensoes_filtro = []
    
    if excluir_diretorios is None:
        excluir_diretorios = ['.git', '__pycache__', 'node_modules', '.venv', 'env']
    
    estrutura = {
        "diretorio_base": diretorio_base,
        "arquivos": []
    }
    
    arquivos_processados = 0
    erros = 0
    
    # Usando os.walk para percorrer recursivamente
    for raiz, diretorios, arquivos in os.walk(diretorio_base):
        
        # Remove diretórios que devem ser excluídos
        diretorios[:] = [d for d in diretorios if d not in excluir_diretorios]
        
        for arquivo in arquivos:
            caminho_completo = os.path.join(raiz, arquivo)
            caminho_relativo = os.path.relpath(caminho_completo, diretorio_base)
            
            # Verifica se deve filtrar por extensão
            if extensoes_filtro:
                extensao = os.path.splitext(arquivo)[1].lower()
                if extensao not in extensoes_filtro:
                    continue
            
            print(f"Processando: {caminho_relativo}")
            
            # Lê o conteúdo do arquivo
            conteudo, tipo_conteudo = ler_arquivo(caminho_completo)
            
            # Obtém informações do arquivo
            try:
                stats = os.stat(caminho_completo)
                info_arquivo = {
                    "nome": arquivo,
                    "caminho_absoluto": caminho_completo,
                    "caminho_relativo": caminho_relativo,
                    "diretorio": raiz,
                    "extensao": os.path.splitext(arquivo)[1].lower(),
                    "tamanho_bytes": stats.st_size,
                    "ultima_modificacao": stats.st_mtime,
                    "tipo_conteudo": tipo_conteudo,
                    "conteudo": conteudo
                }
                
                estrutura["arquivos"].append(info_arquivo)
                arquivos_processados += 1
                
            except Exception as e:
                print(f"Erro ao obter informações do arquivo {caminho_completo}: {e}")
                erros += 1
    
    estrutura["resumo"] = {
        "total_arquivos_processados": arquivos_processados,
        "total_erros": erros,
        "diretorios_percorridos": len(set([os.path.dirname(a["caminho_absoluto"]) for a in estrutura["arquivos"]]))
    }
    
    print(f"\nProcessamento concluído!")
    print(f"Arquivos processados: {arquivos_processados}")
    print(f"Erros encontrados: {erros}")
    
    return estrutura

def salvar_em_json(estrutura, arquivo_saida):
    """
    Salva a estrutura do diretório em um arquivo JSON.
    """
    try:
        with open(arquivo_saida, 'w', encoding='utf-8') as f:
            json.dump(estrutura, f, ensure_ascii=False, indent=2)
        print(f"Dados salvos com sucesso em: {arquivo_saida}")
        print(f"Tamanho do arquivo JSON: {os.path.getsize(arquivo_saida)} bytes")
        return True
    except Exception as e:
        print(f"Erro ao salvar arquivo JSON: {e}")
        return False

def salvar_json_comprimido(estrutura, arquivo_saida):
    """
    Salva a estrutura em um arquivo JSON comprimido (sem indentação).
    """
    try:
        with open(arquivo_saida, 'w', encoding='utf-8') as f:
            json.dump(estrutura, f, ensure_ascii=False, separators=(',', ':'))
        print(f"Dados comprimidos salvos em: {arquivo_saida}")
        print(f"Tamanho do arquivo compactado: {os.path.getsize(arquivo_saida)} bytes")
        return True
    except Exception as e:
        print(f"Erro ao salvar JSON comprimido: {e}")
        return False

def carregar_json(arquivo_json):
    """
    Carrega um arquivo JSON gerado pela função salvar_em_json.
    """
    try:
        with open(arquivo_json, 'r', encoding='utf-8') as f:
            dados = json.load(f)
        print(f"Arquivo JSON carregado com sucesso: {arquivo_json}")
        return dados
    except Exception as e:
        print(f"Erro ao carregar arquivo JSON: {e}")
        return None

def exibir_resumo(estrutura):
    """
    Exibe um resumo detalhado da estrutura processada.
    """
    print("\n" + "="*60)
    print("RESUMO DA ESTRUTURA PROCESSADA")
    print("="*60)
    print(f"Diretório base: {estrutura['diretorio_base']}")
    print(f"Total de arquivos: {estrutura['resumo']['total_arquivos_processados']}")
    print(f"Total de erros: {estrutura['resumo']['total_erros']}")
    print(f"Diretórios percorridos: {estrutura['resumo']['diretorios_percorridos']}")
    
    # Contagem por tipo de arquivo
    tipos = {}
    for arquivo in estrutura["arquivos"]:
        ext = arquivo["extensao"] or "sem_extensao"
        tipos[ext] = tipos.get(ext, 0) + 1
    
    print("\nTipos de arquivo encontrados:")
    for ext, quantidade in sorted(tipos.items()):
        print(f"  {ext}: {quantidade} arquivo(s)")
    
    # Mostra alguns exemplos
    print("\nPrimeiros 5 arquivos processados:")
    for i, arquivo in enumerate(estrutura["arquivos"][:5]):
        print(f"  {i+1}. {arquivo['caminho_relativo']} ({arquivo['tamanho_bytes']} bytes)")

# Exemplo de uso
if __name__ == "__main__":
    # Configurações
    diretorio_para_ler = "."  # Altere para o diretório desejado
    
    # Opcionais: filtrar por extensões específicas
    extensoes_filtro = ['.txt']  # Descomente para filtrar
    
    # Opcionais: diretórios para excluir
    excluir_dirs = ['__pycache__', 'assets', 'dist', '.git', 'venv', 'logs', 'build', 'uploads', '.vscode', 'thirdparty', 'test', 'ThirdParty', 'tools', 'resources', 'bin']
    
    print(f"Lendo diretório: {os.path.abspath(diretorio_para_ler)}")
    
    # Percorre o diretório
    dados_estrutura = percorrer_diretorio(
        diretorio_para_ler,
        #extensoes_filtro=extensoes_filtro,
        excluir_diretorios=excluir_dirs
    )
    
    # Salva em JSON
    if dados_estrutura["arquivos"]:
        arquivo_json = "estrutura_diretorio.json"
        salvar_em_json(dados_estrutura, arquivo_json)
        
        # Opcional: salvar versão comprimida
        # salvar_json_comprimido(dados_estrutura, "estrutura_comprimida.json")
        
        # Exibe resumo
        exibir_resumo(dados_estrutura)
        
        # Exemplo de como carregar o JSON depois
        # dados_carregados = carregar_json(arquivo_json)
    else:
        print("Nenhum arquivo foi processado!")