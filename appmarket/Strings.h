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
	kPackagesCountCapped, // "Showing %d of %d packages" -- shown, then total matched
	kSearchLabel,
	kInstalled,       // label inside the rounded "installed" row badge
	kInstall,
	kUninstall,
	kInstalling,      // "Installing %s..."
	kRemoving,        // "Removing %s..."
	kInstallingShort, // per-row button label while that row's install is in flight
	kRemovingShort,   // per-row button label while that row's uninstall is in flight
	kInstalledDone,   // "%s: installed."
	kRemovedDone,     // "%s: removed."
	kFailed,          // "%s: failed -- see Terminal/pkgman for details."
	kFilterLabel,
	kFilterAllRepos,
	kSortLabel,
	kSortByName,
	kSortByRepo,
	kNoDescription,
	kMenuFile,             // "File" menu title
	kMenuQuit,             // "Quit" menu item
	kMenuRepos,            // "Repos" menu title
	kMenuAddRepo,          // "Add Repo..." menu item
	kMenuRemoveRepo,       // "Remove Repo" submenu title
	kMenuRemoveRepoEmpty,  // disabled placeholder when there are no custom repos
	kAddRepoWindowTitle,
	kAddRepoURLLabel,
	kAddRepoButton,
	kCancelButton,
	kAddDesktopIconQuestion, // "Add \"%s\" to the Desktop?" -- asked after a successful install
	kNoButton,
	kAddToDesktopButton,
	kStringCount
};

// Order: en, ko, ja, it, fr, es
static const char* kTable[kStringCount][6] = {
	/* kWindowTitle    */ {"R AppMarket", "R AppMarket", "R AppMarket", "R AppMarket", "R AppMarket", "R AppMarket"},
	/* kLoading        */ {"Loading...", "불러오는 중...", "読み込み中...", "Caricamento...", "Chargement...", "Cargando..."},
	/* kFetchingList    */ {"Fetching package list...", "패키지 목록을 가져오는 중...", "パッケージ一覧を取得中...", "Recupero elenco pacchetti...", "Récupération de la liste des paquets...", "Obteniendo la lista de paquetes..."},
	/* kCouldNotReach  */ {"Could not reach the repository.", "저장소에 연결할 수 없습니다.", "リポジトリに接続できませんでした。", "Impossibile raggiungere il repository.", "Impossible de joindre le dépôt.", "No se pudo contactar con el repositorio."},
	/* kPackagesCount  */ {"%d packages", "패키지 %d개", "%d個のパッケージ", "%d pacchetti", "%d paquets", "%d paquetes"},
	/* kPackagesCountCapped */ {"Showing %d of %d packages", "%d개 표시 중 (전체 %d개)", "%d件表示中 (全%d件)", "Mostrati %d di %d pacchetti", "Affichage de %d sur %d paquets", "Mostrando %d de %d paquetes"},
	/* kSearchLabel    */ {"Search:", "검색:", "検索:", "Cerca:", "Recherche :", "Buscar:"},
	/* kInstalled      */ {"installed", "설치됨", "インストール済み", "installato", "installé", "instalado"},
	/* kInstall        */ {"Install", "설치", "インストール", "Installa", "Installer", "Instalar"},
	/* kUninstall      */ {"Uninstall", "제거", "アンインストール", "Disinstalla", "Désinstaller", "Desinstalar"},
	/* kInstalling     */ {"Installing %s...", "%s 설치 중...", "%sをインストール中...", "Installazione di %s...", "Installation de %s...", "Instalando %s..."},
	/* kRemoving       */ {"Removing %s...", "%s 제거 중...", "%sを削除中...", "Rimozione di %s...", "Suppression de %s...", "Eliminando %s..."},
	/* kInstallingShort */ {"Installing...", "설치 중...", "インストール中...", "Installazione...", "Installation...", "Instalando..."},
	/* kRemovingShort   */ {"Removing...", "제거 중...", "削除中...", "Rimozione...", "Suppression...", "Eliminando..."},
	/* kInstalledDone  */ {"%s: installed.", "%s: 설치됨.", "%s: インストール完了。", "%s: installato.", "%s : installé.", "%s: instalado."},
	/* kRemovedDone    */ {"%s: removed.", "%s: 제거됨.", "%s: 削除完了。", "%s: rimosso.", "%s : supprimé.", "%s: eliminado."},
	/* kFailed         */ {"%s: failed -- see Terminal/pkgman for details.", "%s: 실패했습니다 -- 자세한 내용은 Terminal/pkgman을 확인하세요.", "%s: 失敗しました -- 詳細はTerminal/pkgmanを確認してください。", "%s: non riuscito -- vedi Terminal/pkgman per i dettagli.", "%s : échec -- voir Terminal/pkgman pour plus de détails.", "%s: fallo -- consulte Terminal/pkgman para más detalles."},
	/* kFilterLabel    */ {"Repo:", "저장소:", "リポジトリ:", "Repository:", "Dépôt :", "Repositorio:"},
	/* kFilterAllRepos */ {"All repos", "모든 저장소", "すべてのリポジトリ", "Tutti i repository", "Tous les dépôts", "Todos los repositorios"},
	/* kSortLabel      */ {"Sort:", "정렬:", "並べ替え:", "Ordina:", "Trier :", "Ordenar:"},
	/* kSortByName     */ {"Package name", "패키지 이름", "パッケージ名", "Nome pacchetto", "Nom du paquet", "Nombre del paquete"},
	/* kSortByRepo     */ {"Repo", "저장소", "リポジトリ", "Repository", "Dépôt", "Repositorio"},
	/* kNoDescription  */ {"No description available.", "설명이 없습니다.", "説明はありません。", "Nessuna descrizione disponibile.", "Aucune description disponible.", "No hay descripción disponible."},
	/* kMenuFile            */ {"File", "파일", "ファイル", "File", "Fichier", "Archivo"},
	/* kMenuQuit            */ {"Quit", "종료", "終了", "Esci", "Quitter", "Salir"},
	/* kMenuRepos           */ {"Repos", "저장소", "リポジトリ", "Repository", "Dépôts", "Repositorios"},
	/* kMenuAddRepo         */ {"Add Repo...", "저장소 추가...", "リポジトリを追加...", "Aggiungi repository...", "Ajouter un dépôt...", "Añadir repositorio..."},
	/* kMenuRemoveRepo      */ {"Remove Repo", "저장소 삭제", "リポジトリを削除", "Rimuovi repository", "Supprimer un dépôt", "Eliminar repositorio"},
	/* kMenuRemoveRepoEmpty */ {"No custom repos", "추가한 저장소 없음", "追加したリポジトリなし", "Nessun repository personalizzato", "Aucun dépôt personnalisé", "Sin repositorios personalizados"},
	/* kAddRepoWindowTitle  */ {"Add Repository", "저장소 추가", "リポジトリの追加", "Aggiungi repository", "Ajouter un dépôt", "Añadir repositorio"},
	/* kAddRepoURLLabel     */ {"Repository URL:", "저장소 URL:", "リポジトリURL:", "URL del repository:", "URL du dépôt :", "URL del repositorio:"},
	/* kAddRepoButton       */ {"Add", "추가", "追加", "Aggiungi", "Ajouter", "Añadir"},
	/* kCancelButton        */ {"Cancel", "취소", "キャンセル", "Annulla", "Annuler", "Cancelar"},
	/* kAddDesktopIconQuestion */ {"Add \"%s\" to the Desktop?", "\"%s\"을(를) 데스크탑에 추가할까요?", "\"%s\" をデスクトップに追加しますか?", "Aggiungere \"%s\" alla Scrivania?", "Ajouter \"%s\" au Bureau ?", "¿Añadir \"%s\" al Escritorio?"},
	/* kNoButton            */ {"No", "아니요", "いいえ", "No", "Non", "No"},
	/* kAddToDesktopButton  */ {"Add to Desktop", "데스크탑에 추가", "デスクトップに追加", "Aggiungi alla Scrivania", "Ajouter au Bureau", "Añadir al Escritorio"},
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

// Replaces the first two "%d" placeholders, in order. Every translation of
// a two-placeholder key is written so its first %d is value1 and its
// second is value2, even where the language's own word order would put
// the numbers the other way round -- see kPackagesCountCapped.
inline BString
TFInt2(StringKey key, int value1, int value2)
{
	BString result(T(key));
	BString v1;
	v1 << value1;
	result.ReplaceFirst("%d", v1);
	BString v2;
	v2 << value2;
	result.ReplaceFirst("%d", v2);
	return result;
}

} // namespace rappmarket
