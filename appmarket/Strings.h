// UI text for R AppMarket in six languages, picked at startup from the
// first language in Locale preferences this table covers (same approach
// RSMB uses) -- falls back to English.
#pragma once

#include <LocaleRoster.h>
#include <Message.h>
#include <String.h>

namespace rappmarket {

enum StringKey {
	kWindowTitle = 0,
	kLoading,
	kFetchingList,
	kCouldNotReach,
	kPackagesCount,   // "%d packages"
	kInstalled,       // "(installed)" suffix
	kRefresh,
	kInstall,
	kUninstall,
	kInstalling,      // "Installing %s..."
	kRemoving,        // "Removing %s..."
	kInstalledDone,   // "%s: installed."
	kRemovedDone,     // "%s: removed."
	kFailed,          // "%s: failed -- see Terminal/pkgman for details."
	kColumnRepo,
	kColumnPackage,
	kFilterLabel,
	kFilterAllRepos,
	kSortLabel,
	kSortByName,
	kSortByRepo,
	kStringCount
};

// Order: en, ko, ja, it, fr, es
static const char* kTable[kStringCount][6] = {
	/* kWindowTitle    */ {"R AppMarket", "R AppMarket", "R AppMarket", "R AppMarket", "R AppMarket", "R AppMarket"},
	/* kLoading        */ {"Loading...", "불러오는 중...", "読み込み中...", "Caricamento...", "Chargement...", "Cargando..."},
	/* kFetchingList    */ {"Fetching package list...", "패키지 목록을 가져오는 중...", "パッケージ一覧を取得中...", "Recupero elenco pacchetti...", "Récupération de la liste des paquets...", "Obteniendo la lista de paquetes..."},
	/* kCouldNotReach  */ {"Could not reach the repository.", "저장소에 연결할 수 없습니다.", "リポジトリに接続できませんでした。", "Impossibile raggiungere il repository.", "Impossible de joindre le dépôt.", "No se pudo contactar con el repositorio."},
	/* kPackagesCount  */ {"%d packages", "패키지 %d개", "%d個のパッケージ", "%d pacchetti", "%d paquets", "%d paquetes"},
	/* kInstalled      */ {"(installed)", "(설치됨)", "(インストール済み)", "(installato)", "(installé)", "(instalado)"},
	/* kRefresh        */ {"Refresh", "새로고침", "更新", "Aggiorna", "Actualiser", "Actualizar"},
	/* kInstall        */ {"Install", "설치", "インストール", "Installa", "Installer", "Instalar"},
	/* kUninstall      */ {"Uninstall", "제거", "アンインストール", "Disinstalla", "Désinstaller", "Desinstalar"},
	/* kInstalling     */ {"Installing %s...", "%s 설치 중...", "%sをインストール中...", "Installazione di %s...", "Installation de %s...", "Instalando %s..."},
	/* kRemoving       */ {"Removing %s...", "%s 제거 중...", "%sを削除中...", "Rimozione di %s...", "Suppression de %s...", "Eliminando %s..."},
	/* kInstalledDone  */ {"%s: installed.", "%s: 설치됨.", "%s: インストール完了。", "%s: installato.", "%s : installé.", "%s: instalado."},
	/* kRemovedDone    */ {"%s: removed.", "%s: 제거됨.", "%s: 削除完了。", "%s: rimosso.", "%s : supprimé.", "%s: eliminado."},
	/* kFailed         */ {"%s: failed -- see Terminal/pkgman for details.", "%s: 실패했습니다 -- 자세한 내용은 Terminal/pkgman을 확인하세요.", "%s: 失敗しました -- 詳細はTerminal/pkgmanを確認してください。", "%s: non riuscito -- vedi Terminal/pkgman per i dettagli.", "%s : échec -- voir Terminal/pkgman pour plus de détails.", "%s: fallo -- consulte Terminal/pkgman para más detalles."},
	/* kColumnRepo     */ {"Repo", "저장소", "リポジトリ", "Repository", "Dépôt", "Repositorio"},
	/* kColumnPackage  */ {"Package", "패키지", "パッケージ", "Pacchetto", "Paquet", "Paquete"},
	/* kFilterLabel    */ {"Repo:", "저장소:", "リポジトリ:", "Repository:", "Dépôt :", "Repositorio:"},
	/* kFilterAllRepos */ {"All repos", "모든 저장소", "すべてのリポジトリ", "Tutti i repository", "Tous les dépôts", "Todos los repositorios"},
	/* kSortLabel      */ {"Sort:", "정렬:", "並べ替え:", "Ordina:", "Trier :", "Ordenar:"},
	/* kSortByName     */ {"Package name", "패키지 이름", "パッケージ名", "Nome pacchetto", "Nom du paquet", "Nombre del paquete"},
	/* kSortByRepo     */ {"Repo", "저장소", "リポジトリ", "Repository", "Dépôt", "Repositorio"},
};

static const char* kLanguageCodes[6] = {"en", "ko", "ja", "it", "fr", "es"};

static int sLanguageIndex = -1;

inline int
DetectLanguageIndex()
{
	BMessage preferred;
	if (BLocaleRoster::Default()->GetPreferredLanguages(&preferred) == B_OK) {
		BString lang;
		for (int32 i = 0; preferred.FindString("language", i, &lang) == B_OK; i++) {
			for (int l = 1; l < 6; l++) {
				if (lang.IFindFirst(kLanguageCodes[l]) == 0)
					return l;
			}
		}
	}
	return 0; // English
}

inline const char*
T(StringKey key)
{
	if (sLanguageIndex < 0)
		sLanguageIndex = DetectLanguageIndex();
	return kTable[key][sLanguageIndex];
}

inline BString
TF(StringKey key, const char* arg)
{
	BString result(T(key));
	BString placeholder("%s");
	result.ReplaceFirst(placeholder, arg);
	return result;
}

inline BString
TFInt(StringKey key, int value)
{
	BString result(T(key));
	BString valueStr;
	valueStr << value;
	result.ReplaceFirst("%d", valueStr);
	return result;
}

} // namespace rappmarket
