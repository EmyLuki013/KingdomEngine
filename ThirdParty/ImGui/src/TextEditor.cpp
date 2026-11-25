#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <regex>
#include <cmath>

#include "TextEditor.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h" // for ImGui rendering helpers

static bool TextEditor_IsIdentifierByte(unsigned char c)
{
	return c >= 0x80 || c == '_' || std::isalnum(c) != 0;
}

static unsigned char TextEditor_FoldASCII(unsigned char c)
{
	return c < 0x80 ? (unsigned char)std::tolower(c) : c;
}

static bool TextEditor_StartsWithCaseInsensitive(const std::string& aText, const std::string& aPrefix)
{
	if (aText.size() < aPrefix.size())
		return false;
	for (size_t i = 0; i < aPrefix.size(); ++i)
	{
		if (TextEditor_FoldASCII((unsigned char)aText[i]) != TextEditor_FoldASCII((unsigned char)aPrefix[i]))
			return false;
	}
	return true;
}

bool TextEditor::HandleAutoClose(ImWchar aChar)
{
	if (mReadOnly || mOverwrite || HasSelection() || aChar > 0x7f)
		return false;

	char c = (char)aChar;
	auto pos = GetActualCursorCoordinates();
	auto cindex = GetCharacterIndex(pos);
	auto& line = mLines[pos.mLine];
	char next = (cindex < (int)line.size()) ? (char)line[cindex].mChar : '\0';

	auto inCommentOrString = [&](int index)
	{
		if (index < 0 || index >= (int)line.size())
			return false;
		const auto& glyph = line[index];
		return glyph.mComment || glyph.mMultiLineComment ||
			glyph.mColorIndex == PaletteIndex::String ||
			glyph.mColorIndex == PaletteIndex::CharLiteral;
	};
	if ((c == '"' || c == '\'') && next == c)
	{
		MoveRight(1, false);
		return true;
	}
	if (inCommentOrString(cindex - 1))
		return false;

	struct Pair { char open, close; };
	static const Pair pairs[] = { {'(', ')'}, {'[', ']'}, {'{', '}'} };
	for (auto& p : pairs)
	{
		if (c == p.open)
		{
			EnterCharacter(aChar, false);
			if (next != p.close)
			{
				EnterCharacter(p.close, false);
				MoveLeft(1, false);
			}
			return true;
		}
		if (c == p.close && next == c)
		{
			MoveRight(1, false);
			return true;
		}
	}

	if (c == '"' || c == '\'')
	{
		EnterCharacter(aChar, false);
		EnterCharacter(aChar, false);
		MoveLeft(1, false);
		return true;
	}
	return false;
}

// TODO
// - multiline comments vs single-line: latter is blocking start of a ML

static int TextEditor_BracketDepth(char c)
{
	switch (c)
	{
	case '(': case ')': return 1;
	case '[': case ']': return 2;
	case '{': case '}': return 3;
	}
	return 0;
}

int TextEditor::BracketDepth(char c) { return TextEditor_BracketDepth(c); }

TextEditor::Coordinates TextEditor::GetMatchingBracket(const Coordinates& aFrom) const
{
	if (aFrom.mLine < 0 || aFrom.mLine >= (int)mLines.size())
		return Coordinates::Invalid();

	auto& line = mLines[aFrom.mLine];
	auto cindex = GetCharacterIndex(aFrom);
	if (cindex < 0 || cindex >= (int)line.size())
		return Coordinates::Invalid();

	char c = (char)line[cindex].mChar;
	if (BracketDepth(c) == 0)
		return Coordinates::Invalid();

	bool forward = (c == '(' || c == '[' || c == '{');
	auto corresponding = [](char bracket, bool opening)
	{
		switch (bracket)
		{
		case '(': return opening ? ')' : '(';
		case '[': return opening ? ']' : '[';
		case '{': return opening ? '}' : '{';
		case ')': return opening ? ')' : '(';
		case ']': return opening ? ']' : '[';
		case '}': return opening ? '}' : '{';
		}
		return '\0';
	};
	auto isCodeBracket = [&](int lineIndex, int glyphIndex)
	{
		const auto& glyph = mLines[lineIndex][glyphIndex];
		if (glyph.mComment || glyph.mMultiLineComment ||
			glyph.mColorIndex == PaletteIndex::String ||
			glyph.mColorIndex == PaletteIndex::CharLiteral)
			return false;
		return BracketDepth((char)glyph.mChar) != 0;
	};

	std::vector<char> expected;
	expected.push_back(corresponding(c, forward));
	int lineIndex = aFrom.mLine;
	int glyphIndex = cindex + (forward ? 1 : -1);

	while (lineIndex >= 0 && lineIndex < (int)mLines.size())
	{
		const auto& currentLine = mLines[lineIndex];
		while (glyphIndex >= 0 && glyphIndex < (int)currentLine.size())
		{
			if (isCodeBracket(lineIndex, glyphIndex))
			{
				const char current = (char)currentLine[glyphIndex].mChar;
				if (forward)
				{
					if (current == '(' || current == '[' || current == '{')
						expected.push_back(corresponding(current, true));
					else if (current != expected.back())
						return Coordinates::Invalid();
					else
					{
						expected.pop_back();
						if (expected.empty())
							return Coordinates(lineIndex, GetCharacterColumn(lineIndex, glyphIndex));
					}
				}
				else if (current == ')' || current == ']' || current == '}')
					expected.push_back(corresponding(current, false));
				else if (current != expected.back())
					return Coordinates::Invalid();
				else
				{
					expected.pop_back();
					if (expected.empty())
						return Coordinates(lineIndex, GetCharacterColumn(lineIndex, glyphIndex));
				}
			}
			glyphIndex += forward ? 1 : -1;
		}
		lineIndex += forward ? 1 : -1;
		if (lineIndex >= 0 && lineIndex < (int)mLines.size())
			glyphIndex = forward ? 0 : (int)mLines[lineIndex].size() - 1;
	}
	return Coordinates::Invalid();
}

void TextEditor::JumpToMatchingBracket()
{
	auto pos = GetActualCursorCoordinates();
	auto match = GetMatchingBracket(pos);
	// Se não achou no cursor, tenta no caractere anterior
	if (match.mLine < 0 && pos.mColumn > 0)
	{
		auto prev = pos;
		prev.mColumn--;
		match = GetMatchingBracket(prev);
	}
	if (match.mLine >= 0)
	{
		SetCursorPosition(match);
		SetSelection(match, match);
		mInteractiveStart = mInteractiveEnd = match;
	}
}

void TextEditor::RenderBracketHighlight()
{
	auto pos = GetActualCursorCoordinates();
	Coordinates targets[2] = { Coordinates::Invalid(), Coordinates::Invalid() };

	auto m1 = GetMatchingBracket(pos);
	if (m1.mLine >= 0)
	{
		targets[0] = pos;
		targets[1] = m1;
	}
	else if (pos.mColumn > 0)
	{
		auto prev = pos; prev.mColumn--;
		auto m2 = GetMatchingBracket(prev);
		if (m2.mLine >= 0) { targets[0] = prev; targets[1] = m2; }
	}

	if (targets[0].mLine < 0) return;

	auto drawList = ImGui::GetWindowDrawList();
	ImVec2 origin = ImGui::GetCursorScreenPos();
	float scrollX = ImGui::GetScrollX();
	float scrollY = ImGui::GetScrollY();

	for (int i = 0; i < 2; ++i)
	{
		auto& t = targets[i];
		if (t.mLine < 0 || t.mLine >= (int)mLines.size()) continue;
		float x = TextDistanceToLineStart(t);
		ImVec2 p1(origin.x + mTextStart + x - scrollX, origin.y + t.mLine * mCharAdvance.y - scrollY);
		ImVec2 p2(p1.x + mCharAdvance.x, p1.y + mCharAdvance.y);
		drawList->AddRect(p1, p2, mPalette[(int)PaletteIndex::Keyword], 0.0f, 0, 1.5f);
	}
}

void TextEditor::RenderFindHighlights()
{
	if (!mFindOpen || mFindMatchStarts.empty())
		return;

	auto drawList = ImGui::GetWindowDrawList();
	ImVec2 origin = ImGui::GetCursorScreenPos();
	float scrollX = ImGui::GetScrollX();
	float scrollY = ImGui::GetScrollY();
	const auto contentSize = ImGui::GetWindowContentRegionMax();
	const int firstVisibleLine = std::max(0, (int)floor(scrollY / mCharAdvance.y));
	const int lastVisibleLine = std::min((int)mLines.size() - 1,
		firstVisibleLine + (int)floor((scrollY + contentSize.y) / mCharAdvance.y));

	for (size_t i = 0; i < mFindMatchStarts.size(); ++i)
	{
		const auto& start = mFindMatchStarts[i];
		const auto& end = mFindMatchEnds[i];
		if (start.mLine != end.mLine || start.mLine < firstVisibleLine ||
			start.mLine > lastVisibleLine || start.mLine >= (int)mLines.size())
			continue;

		float x1 = TextDistanceToLineStart(start);
		float x2 = TextDistanceToLineStart(end);
		float y = origin.y + start.mLine * mCharAdvance.y - scrollY;
		ImVec2 p1(origin.x + mTextStart + x1 - scrollX, y);
		ImVec2 p2(origin.x + mTextStart + x2 - scrollX, y + mCharAdvance.y);
		drawList->AddRect(p1, p2, mPalette[(int)PaletteIndex::Keyword], 0.0f, 0, 1.5f);
	}
}

std::string TextEditor::GetWordPrefixAtCursor() const
{
	const auto cursor = GetActualCursorCoordinates();
	if (cursor.mLine < 0 || cursor.mLine >= (int)mLines.size())
		return std::string();

	const auto& line = mLines[cursor.mLine];
	const int characterIndex = GetCharacterIndex(cursor);
	if (characterIndex < 0)
		return std::string();
	const int cursorIndex = std::min(characterIndex, (int)line.size());
	int startIndex = cursorIndex;
	while (startIndex > 0 && TextEditor_IsIdentifierByte((unsigned char)line[startIndex - 1].mChar))
		--startIndex;

	std::string prefix;
	prefix.reserve((size_t)(cursorIndex - startIndex));
	for (int i = startIndex; i < cursorIndex; ++i)
		prefix.push_back((char)line[i].mChar);
	return prefix;
}

void TextEditor::TriggerAutocomplete()
{
	if (mReadOnly || HasSelection())
	{
		CloseAutocomplete();
		return;
	}

	mAutocompleteOpen = true;
	UpdateAutocomplete();
}

void TextEditor::CloseAutocomplete()
{
	mAutocompleteOpen = false;
	mAutocompleteItems.clear();
	mAutocompleteSelected = 0;
}

void TextEditor::UpdateAutocomplete()
{
	const auto cursor = GetActualCursorCoordinates();
	const auto prefix = GetWordPrefixAtCursor();
	const bool contextChanged = cursor != mAutocompleteCursor || prefix != mAutocompletePrefix;
	mAutocompleteCursor = cursor;
	mAutocompletePrefix = prefix;
	if (contextChanged)
		mAutocompleteSelected = 0;

	mAutocompleteItems.clear();
	if (mReadOnly || HasSelection())
	{
		CloseAutocomplete();
		return;
	}

	std::unordered_set<std::string> seen;
	auto addCandidate = [&](CompletionItem item)
	{
		if (item.mText.empty() || !TextEditor_StartsWithCaseInsensitive(item.mText, prefix))
			return;
		if (seen.insert(item.mText).second)
			mAutocompleteItems.push_back(std::move(item));
	};

	if (mCompletionProvider)
	{
		std::vector<CompletionItem> externalItems;
		mCompletionProvider(prefix, externalItems);
		for (auto& item : externalItems)
		{
			if (item.mKind == 0)
				item.mKind = 2;
			if (item.mDetail.empty())
				item.mDetail = "reflection";
			addCandidate(std::move(item));
		}
	}

	for (const auto& keyword : mLanguageDefinition.mKeywords)
	{
		CompletionItem item;
		item.mText = keyword;
		item.mDetail = "keyword";
		item.mKind = 0;
		addCandidate(std::move(item));
	}

	for (const auto& identifier : mLanguageDefinition.mIdentifiers)
	{
		CompletionItem item;
		item.mText = identifier.first;
		item.mDetail = identifier.second.mDeclaration.empty() ? "identifier" : identifier.second.mDeclaration;
		item.mDocumentation = identifier.second.mDeclaration;
		item.mKind = 1;
		addCandidate(std::move(item));
	}

	std::unordered_set<std::string> localWords;
	for (const auto& line : mLines)
	{
		for (size_t i = 0; i < line.size();)
		{
			if (!TextEditor_IsIdentifierByte((unsigned char)line[i].mChar))
			{
				++i;
				continue;
			}

			std::string word;
			while (i < line.size() && TextEditor_IsIdentifierByte((unsigned char)line[i].mChar))
				word.push_back((char)line[i++].mChar);
			localWords.insert(std::move(word));
		}
	}
	for (const auto& word : localWords)
	{
		CompletionItem item;
		item.mText = word;
		item.mDetail = "local";
		item.mKind = 1;
		addCandidate(std::move(item));
	}

	std::sort(mAutocompleteItems.begin(), mAutocompleteItems.end(),
		[](const CompletionItem& a, const CompletionItem& b)
		{
			if (a.mKind != b.mKind)
				return a.mKind > b.mKind;
			const auto lessFolded = std::lexicographical_compare(
				a.mText.begin(), a.mText.end(), b.mText.begin(), b.mText.end(),
				[](unsigned char x, unsigned char y)
				{
					return TextEditor_FoldASCII(x) < TextEditor_FoldASCII(y);
				});
			const auto greaterFolded = std::lexicographical_compare(
				b.mText.begin(), b.mText.end(), a.mText.begin(), a.mText.end(),
				[](unsigned char x, unsigned char y)
				{
					return TextEditor_FoldASCII(x) < TextEditor_FoldASCII(y);
				});
			return lessFolded || (!greaterFolded && a.mText < b.mText);
		});

	if (mAutocompleteItems.empty() ||
		(mAutocompleteItems.size() == 1 &&
			TextEditor_StartsWithCaseInsensitive(prefix, mAutocompleteItems.front().mText) &&
			prefix.size() == mAutocompleteItems.front().mText.size()))
	{
		CloseAutocomplete();
		return;
	}
	mAutocompleteOpen = true;
	mAutocompleteSelected = std::min(mAutocompleteSelected, (int)mAutocompleteItems.size() - 1);
}

void TextEditor::ApplyCompletion(const CompletionItem& aItem)
{
	if (mReadOnly || HasSelection())
	{
		CloseAutocomplete();
		return;
	}
	if (aItem.mText.empty())
		return;

	const auto cursor = GetActualCursorCoordinates();
	const auto prefix = GetWordPrefixAtCursor();
	if (prefix.empty())
		return;

	const int cursorIndex = GetCharacterIndex(cursor);
	const int startIndex = cursorIndex - (int)prefix.size();
	if (startIndex < 0 || cursor.mLine < 0 || cursor.mLine >= (int)mLines.size())
		return;
	const auto& line = mLines[cursor.mLine];
	if (startIndex > 0 && ((unsigned char)line[startIndex].mChar & 0xC0) == 0x80)
		return;

	const Coordinates start(cursor.mLine, GetCharacterColumn(cursor.mLine, startIndex));
	UndoRecord undo;
	undo.mBefore = mState;
	undo.mRemovedStart = start;
	undo.mRemovedEnd = cursor;
	undo.mRemoved = prefix;
	DeleteRange(start, cursor);

	Coordinates insertedEnd = start;
	InsertTextAt(insertedEnd, aItem.mText.c_str());
	SetCursorPosition(insertedEnd);
	SetSelection(insertedEnd, insertedEnd);
	mInteractiveStart = mInteractiveEnd = insertedEnd;

	undo.mAddedStart = start;
	undo.mAddedEnd = insertedEnd;
	undo.mAdded = aItem.mText;
	undo.mAfter = mState;
	AddUndo(undo);
	Colorize(std::max(0, start.mLine - 1), 3);
	CloseAutocomplete();
}

void TextEditor::RenderAutocomplete()
{
	if (!mAutocompleteOpen || mAutocompleteItems.empty())
		return;

	const auto cursor = GetActualCursorCoordinates();
	const float x = TextDistanceToLineStart(cursor);
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const float scrollX = ImGui::GetScrollX();
	const float scrollY = ImGui::GetScrollY();
	const ImVec2 screenPos(
		origin.x + mTextStart + x - scrollX + 4.0f,
		origin.y + (cursor.mLine + 1) * mCharAdvance.y - scrollY);

	ImGui::SetNextWindowPos(screenPos, ImGuiCond_Always);
	ImGui::SetNextWindowSizeConstraints(ImVec2(280.0f, 0.0f), ImVec2(600.0f, 260.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
	const ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNavFocus;
	ImGui::Begin("##autocomplete", nullptr, flags);

	bool applied = false;
	for (size_t i = 0; i < mAutocompleteItems.size(); ++i)
	{
		const auto& item = mAutocompleteItems[i];
		const bool selected = (int)i == mAutocompleteSelected;
		const char* kindLabel = item.mKind == 0 ? "[kw]" :
			item.mKind == 2 ? "[refl]" :
			item.mKind == 3 ? "[snip]" : "[id]";
		const std::string label = std::string(kindLabel) + " " + item.mText;

		ImGui::PushID((int)i);
		ImGui::PushStyleColor(ImGuiCol_Text,
			selected ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
		if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_None, ImVec2(200.0f, 0.0f)))
		{
			const CompletionItem chosen = item;
			ApplyCompletion(chosen);
			applied = true;
		}
		if (!applied && selected)
			ImGui::SetScrollHereY(0.5f);
		if (!applied && ImGui::IsItemHovered() && !item.mDetail.empty())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(item.mDocumentation.empty() ? item.mDetail.c_str() : item.mDocumentation.c_str());
			ImGui::EndTooltip();
		}
		ImGui::PopStyleColor();
		if (!applied && !item.mDetail.empty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("%s", item.mDetail.c_str());
		}
		ImGui::PopID();
		if (applied)
			break;
	}

	ImGui::End();
	ImGui::PopStyleVar();
}

void TextEditor::RenderOccurrences()
{
	if (HasSelection() || mLines.empty())
		return;

	const auto cursor = GetActualCursorCoordinates();
	if (cursor.mLine < 0 || cursor.mLine >= (int)mLines.size())
		return;

	const auto& cursorLine = mLines[cursor.mLine];
	if (cursorLine.empty())
		return;

	int cursorIndex = GetCharacterIndex(cursor);
	if (cursorIndex < 0)
		return;
	int probe = cursorIndex;
	if (probe >= (int)cursorLine.size())
		probe = (int)cursorLine.size() - 1;
	else if (!TextEditor_IsIdentifierByte((unsigned char)cursorLine[probe].mChar))
	{
		if (probe == 0 || !TextEditor_IsIdentifierByte((unsigned char)cursorLine[probe - 1].mChar))
			return;
		--probe;
	}
	if (probe < 0 || !TextEditor_IsIdentifierByte((unsigned char)cursorLine[probe].mChar))
		return;

	int wordStart = probe;
	while (wordStart > 0 && TextEditor_IsIdentifierByte((unsigned char)cursorLine[wordStart - 1].mChar))
		--wordStart;
	int wordEnd = probe + 1;
	while (wordEnd < (int)cursorLine.size() &&
		TextEditor_IsIdentifierByte((unsigned char)cursorLine[wordEnd].mChar))
		++wordEnd;

	std::string word;
	word.reserve((size_t)(wordEnd - wordStart));
	for (int i = wordStart; i < wordEnd; ++i)
		word.push_back((char)cursorLine[i].mChar);
	if (word.size() < 2)
		return;

	const auto drawList = ImGui::GetWindowDrawList();
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const float scrollX = ImGui::GetScrollX();
	const float scrollY = ImGui::GetScrollY();
	const auto contentSize = ImGui::GetWindowContentRegionMax();
	const int firstVisibleLine = std::max(0, (int)floor(scrollY / mCharAdvance.y));
	const int lastVisibleLine = std::min((int)mLines.size() - 1,
		firstVisibleLine + (int)floor((scrollY + contentSize.y) / mCharAdvance.y));
	const ImU32 highlightColor =
		(mPalette[(int)PaletteIndex::Selection] & 0x00FFFFFFu) | 0x38000000u;

	for (int lineIndex = firstVisibleLine; lineIndex <= lastVisibleLine; ++lineIndex)
	{
		const auto& line = mLines[lineIndex];
		if (line.size() < word.size())
			continue;

		std::string text;
		text.reserve(line.size());
		for (const auto& glyph : line)
			text.push_back((char)glyph.mChar);

		size_t match = 0;
		while ((match = text.find(word, match)) != std::string::npos)
		{
			const bool leftBoundary = match == 0 ||
				!TextEditor_IsIdentifierByte((unsigned char)text[match - 1]);
			const size_t matchEnd = match + word.size();
			const bool rightBoundary = matchEnd == text.size() ||
				!TextEditor_IsIdentifierByte((unsigned char)text[matchEnd]);
			if (leftBoundary && rightBoundary)
			{
				const Coordinates start(lineIndex, GetCharacterColumn(lineIndex, (int)match));
				const Coordinates end(lineIndex, GetCharacterColumn(lineIndex, (int)matchEnd));
				const float x1 = TextDistanceToLineStart(start);
				const float x2 = TextDistanceToLineStart(end);
				const float y = origin.y + lineIndex * mCharAdvance.y - scrollY;
				const ImVec2 p1(origin.x + mTextStart + x1 - scrollX, y);
				const ImVec2 p2(origin.x + mTextStart + x2 - scrollX, y + mCharAdvance.y);
				drawList->AddRectFilled(p1, p2, highlightColor);
			}
			match = matchEnd;
		}
	}
}

template<class InputIt1, class InputIt2, class BinaryPredicate>
bool equals(InputIt1 first1, InputIt1 last1,
	InputIt2 first2, InputIt2 last2, BinaryPredicate p)
{
	for (; first1 != last1 && first2 != last2; ++first1, ++first2)
	{
		if (!p(*first1, *first2))
			return false;
	}
	return first1 == last1 && first2 == last2;
}

TextEditor::TextEditor()
	: mLineSpacing(1.0f)
	, mUndoIndex(0)
	, mTabSize(4)
	, mOverwrite(false)
	, mReadOnly(false)
	, mWithinRender(false)
	, mScrollToCursor(false)
	, mScrollToTop(false)
	, mTextChanged(false)
	, mColorizerEnabled(true)
	, mTextStart(20.0f)
	, mLeftMargin(10)
	, mCursorPositionChanged(false)
	, mColorRangeMin(0)
	, mColorRangeMax(0)
	, mSelectionMode(SelectionMode::Normal)
	, mCheckComments(true)
	, mLastClick(-1.0f)
	, mHandleKeyboardInputs(true)
	, mHandleMouseInputs(true)
	, mIgnoreImGuiChild(false)
	, mShowWhitespaces(true)
	, mStartTime(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count())
{
	SetPalette(GetDarkPalette());
	SetLanguageDefinition(LanguageDefinition::HLSL());
	mLines.push_back(Line());
}

TextEditor::~TextEditor()
{
}

void TextEditor::SetLanguageDefinition(const LanguageDefinition & aLanguageDef)
{
	mLanguageDefinition = aLanguageDef;
	mRegexList.clear();

	for (auto& r : mLanguageDefinition.mTokenRegexStrings)
		mRegexList.push_back(std::make_pair(std::regex(r.first, std::regex_constants::optimize), r.second));

	Colorize();
}

void TextEditor::SetPalette(const Palette & aValue)
{
	mPaletteBase = aValue;
}

std::string TextEditor::GetText(const Coordinates & aStart, const Coordinates & aEnd) const
{
	std::string result;

	auto lstart = aStart.mLine;
	auto lend = aEnd.mLine;
	auto istart = GetCharacterIndex(aStart);
	auto iend = GetCharacterIndex(aEnd);
	size_t s = 0;

	for (size_t i = lstart; i < lend; i++)
		s += mLines[i].size();

	result.reserve(s + s / 8);

	while (istart < iend || lstart < lend)
	{
		if (lstart >= (int)mLines.size())
			break;

		auto& line = mLines[lstart];
		if (istart < (int)line.size())
		{
			result += line[istart].mChar;
			istart++;
		}
		else
		{
			istart = 0;
			++lstart;
			result += '\n';
		}
	}

	return result;
}

TextEditor::Coordinates TextEditor::GetActualCursorCoordinates() const
{
	return SanitizeCoordinates(mState.mCursorPosition);
}

TextEditor::Coordinates TextEditor::SanitizeCoordinates(const Coordinates & aValue) const
{
	auto line = aValue.mLine;
	auto column = aValue.mColumn;
	if (line >= (int)mLines.size())
	{
		if (mLines.empty())
		{
			line = 0;
			column = 0;
		}
		else
		{
			line = (int)mLines.size() - 1;
			column = GetLineMaxColumn(line);
		}
		return Coordinates(line, column);
	}
	else
	{
		column = mLines.empty() ? 0 : std::min(column, GetLineMaxColumn(line));
		return Coordinates(line, column);
	}
}

// https://en.wikipedia.org/wiki/UTF-8
// We assume that the char is a standalone character (<128) or a leading byte of an UTF-8 code sequence (non-10xxxxxx code)
static int UTF8CharLength(TextEditor::Char c)
{
	if ((c & 0xF8) == 0xF0 && c <= 0xF4)
		return 4;
	else if ((c & 0xF0) == 0xE0)
		return 3;
	else if ((c & 0xE0) == 0xC0 && c >= 0xC2)
		return 2;
	return 1;
}

// "Borrowed" from ImGui source
static inline int ImTextCharToUtf8(char* buf, int buf_size, unsigned int c)
{
	if (c < 0x80)
	{
		buf[0] = (char)c;
		return 1;
	}
	if (c < 0x800)
	{
		if (buf_size < 2) return 0;
		buf[0] = (char)(0xc0 + (c >> 6));
		buf[1] = (char)(0x80 + (c & 0x3f));
		return 2;
	}
	if (c >= 0xdc00 && c < 0xe000)
	{
		return 0;
	}
	if (c >= 0xd800 && c < 0xdc00)
	{
		if (buf_size < 4) return 0;
		buf[0] = (char)(0xf0 + (c >> 18));
		buf[1] = (char)(0x80 + ((c >> 12) & 0x3f));
		buf[2] = (char)(0x80 + ((c >> 6) & 0x3f));
		buf[3] = (char)(0x80 + ((c) & 0x3f));
		return 4;
	}
	//else if (c < 0x10000)
	{
		if (buf_size < 3) return 0;
		buf[0] = (char)(0xe0 + (c >> 12));
		buf[1] = (char)(0x80 + ((c >> 6) & 0x3f));
		buf[2] = (char)(0x80 + ((c) & 0x3f));
		return 3;
	}
}

void TextEditor::Advance(Coordinates & aCoordinates) const
{
	if (aCoordinates.mLine < (int)mLines.size())
	{
		auto& line = mLines[aCoordinates.mLine];
		auto cindex = GetCharacterIndex(aCoordinates);

		if (cindex + 1 < (int)line.size())
		{
			auto delta = UTF8CharLength(line[cindex].mChar);
			cindex = std::min(cindex + delta, (int)line.size() - 1);
		}
		else
		{
			++aCoordinates.mLine;
			cindex = 0;
		}
		aCoordinates.mColumn = GetCharacterColumn(aCoordinates.mLine, cindex);
	}
}

void TextEditor::DeleteRange(const Coordinates & aStart, const Coordinates & aEnd)
{
	assert(aEnd >= aStart);
	assert(!mReadOnly);

	//printf("D(%d.%d)-(%d.%d)\n", aStart.mLine, aStart.mColumn, aEnd.mLine, aEnd.mColumn);

	if (aEnd == aStart)
		return;

	auto start = GetCharacterIndex(aStart);
	auto end = GetCharacterIndex(aEnd);

	if (aStart.mLine == aEnd.mLine)
	{
		auto& line = mLines[aStart.mLine];
		auto n = GetLineMaxColumn(aStart.mLine);
		if (aEnd.mColumn >= n)
			line.erase(line.begin() + start, line.end());
		else
			line.erase(line.begin() + start, line.begin() + end);
	}
	else
	{
		auto& firstLine = mLines[aStart.mLine];
		auto& lastLine = mLines[aEnd.mLine];

		firstLine.erase(firstLine.begin() + start, firstLine.end());
		lastLine.erase(lastLine.begin(), lastLine.begin() + end);

		if (aStart.mLine < aEnd.mLine)
			firstLine.insert(firstLine.end(), lastLine.begin(), lastLine.end());

		if (aStart.mLine < aEnd.mLine)
			RemoveLine(aStart.mLine + 1, aEnd.mLine + 1);
	}

	mTextChanged = true;
}

int TextEditor::InsertTextAt(Coordinates& /* inout */ aWhere, const char * aValue)
{
	assert(!mReadOnly);

	int cindex = GetCharacterIndex(aWhere);
	int totalLines = 0;
	while (*aValue != '\0')
	{
		assert(!mLines.empty());

		if (*aValue == '\r')
		{
			// skip
			++aValue;
		}
		else if (*aValue == '\n')
		{
			if (cindex < (int)mLines[aWhere.mLine].size())
			{
				auto& newLine = InsertLine(aWhere.mLine + 1);
				auto& line = mLines[aWhere.mLine];
				newLine.insert(newLine.begin(), line.begin() + cindex, line.end());
				line.erase(line.begin() + cindex, line.end());
			}
			else
			{
				InsertLine(aWhere.mLine + 1);
			}
			++aWhere.mLine;
			aWhere.mColumn = 0;
			cindex = 0;
			++totalLines;
			++aValue;
		}
		else
		{
			auto& line = mLines[aWhere.mLine];
			auto d = UTF8CharLength(*aValue);
			while (d-- > 0 && *aValue != '\0')
				line.insert(line.begin() + cindex++, Glyph(*aValue++, PaletteIndex::Default));
			++aWhere.mColumn;
		}

		mTextChanged = true;
	}

	return totalLines;
}

void TextEditor::AddUndo(UndoRecord& aValue, bool aMergeWithPrevious)
{
	assert(!mReadOnly);
	//printf("AddUndo: (@%d.%d) +\'%s' [%d.%d .. %d.%d], -\'%s', [%d.%d .. %d.%d] (@%d.%d)\n",
	//	aValue.mBefore.mCursorPosition.mLine, aValue.mBefore.mCursorPosition.mColumn,
	//	aValue.mAdded.c_str(), aValue.mAddedStart.mLine, aValue.mAddedStart.mColumn, aValue.mAddedEnd.mLine, aValue.mAddedEnd.mColumn,
	//	aValue.mRemoved.c_str(), aValue.mRemovedStart.mLine, aValue.mRemovedStart.mColumn, aValue.mRemovedEnd.mLine, aValue.mRemovedEnd.mColumn,
	//	aValue.mAfter.mCursorPosition.mLine, aValue.mAfter.mCursorPosition.mColumn
	//	);

	mUndoBuffer.resize((size_t)mUndoIndex);
	if (aMergeWithPrevious && mUndoIndex > 0)
	{
		auto& previous = mUndoBuffer.back();
		const auto& before = aValue.mBefore;
		if (!previous.mAdded.empty() && previous.mRemoved.empty() &&
			!aValue.mAdded.empty() && aValue.mRemoved.empty() &&
			previous.mAddedEnd == aValue.mAddedStart &&
			previous.mAddedEnd.mLine == aValue.mAddedStart.mLine &&
			previous.mAfter.mCursorPosition == before.mCursorPosition &&
			previous.mAfter.mSelectionStart == before.mSelectionStart &&
			previous.mAfter.mSelectionEnd == before.mSelectionEnd)
		{
			previous.mAdded += aValue.mAdded;
			previous.mAddedEnd = aValue.mAddedEnd;
			previous.mAfter = aValue.mAfter;
			return;
		}
	}

	mUndoBuffer.resize((size_t)(mUndoIndex + 1));
	mUndoBuffer.back() = aValue;
	++mUndoIndex;
}

TextEditor::Coordinates TextEditor::ScreenPosToCoordinates(const ImVec2& aPosition) const
{
	ImVec2 origin = ImGui::GetCursorScreenPos();
	ImVec2 local(aPosition.x - origin.x, aPosition.y - origin.y);

	int lineNo = std::max(0, (int)floor(local.y / mCharAdvance.y));

	int columnCoord = 0;

	if (lineNo >= 0 && lineNo < (int)mLines.size())
	{
		auto& line = mLines.at(lineNo);

		int columnIndex = 0;
		float columnX = 0.0f;

		while ((size_t)columnIndex < line.size())
		{
			float columnWidth = 0.0f;

			if (line[columnIndex].mChar == '\t')
			{
				float spaceSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, " ").x;
				float oldX = columnX;
				float newColumnX = (1.0f + std::floor((1.0f + columnX) / (float(mTabSize) * spaceSize))) * (float(mTabSize) * spaceSize);
				columnWidth = newColumnX - oldX;
				if (mTextStart + columnX + columnWidth * 0.5f > local.x)
					break;
				columnX = newColumnX;
				columnCoord = (columnCoord / mTabSize) * mTabSize + mTabSize;
				columnIndex++;
			}
			else
			{
				char buf[7];
				auto d = UTF8CharLength(line[columnIndex].mChar);
				int i = 0;
				while (i < 6 && d-- > 0)
					buf[i++] = line[columnIndex++].mChar;
				buf[i] = '\0';
				columnWidth = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, buf).x;
				if (mTextStart + columnX + columnWidth * 0.5f > local.x)
					break;
				columnX += columnWidth;
				columnCoord++;
			}
		}
	}

	return SanitizeCoordinates(Coordinates(lineNo, columnCoord));
}

TextEditor::Coordinates TextEditor::FindWordStart(const Coordinates & aFrom) const
{
	Coordinates at = aFrom;
	if (at.mLine >= (int)mLines.size())
		return at;

	auto& line = mLines[at.mLine];
	auto cindex = GetCharacterIndex(at);

	if (cindex >= (int)line.size())
		return at;

	while (cindex > 0 && isspace(line[cindex].mChar))
		--cindex;

	auto cstart = (PaletteIndex)line[cindex].mColorIndex;
	while (cindex > 0)
	{
		auto c = line[cindex].mChar;
		if ((c & 0xC0) != 0x80)	// not UTF code sequence 10xxxxxx
		{
			if (c <= 32 && isspace(c))
			{
				cindex++;
				break;
			}
			if (cstart != (PaletteIndex)line[size_t(cindex - 1)].mColorIndex)
				break;
		}
		--cindex;
	}
	return Coordinates(at.mLine, GetCharacterColumn(at.mLine, cindex));
}

TextEditor::Coordinates TextEditor::FindWordEnd(const Coordinates & aFrom) const
{
	Coordinates at = aFrom;
	if (at.mLine >= (int)mLines.size())
		return at;

	auto& line = mLines[at.mLine];
	auto cindex = GetCharacterIndex(at);

	if (cindex >= (int)line.size())
		return at;

	bool prevspace = (bool)isspace(line[cindex].mChar);
	auto cstart = (PaletteIndex)line[cindex].mColorIndex;
	while (cindex < (int)line.size())
	{
		auto c = line[cindex].mChar;
		auto d = UTF8CharLength(c);
		if (cstart != (PaletteIndex)line[cindex].mColorIndex)
			break;

		if (prevspace != !!isspace(c))
		{
			if (isspace(c))
				while (cindex < (int)line.size() && isspace(line[cindex].mChar))
					++cindex;
			break;
		}
		cindex += d;
	}
	return Coordinates(aFrom.mLine, GetCharacterColumn(aFrom.mLine, cindex));
}

TextEditor::Coordinates TextEditor::FindNextWord(const Coordinates & aFrom) const
{
	Coordinates at = aFrom;
	if (at.mLine >= (int)mLines.size())
		return at;

	// skip to the next non-word character
	auto cindex = GetCharacterIndex(aFrom);
	bool isword = false;
	bool skip = false;
	if (cindex < (int)mLines[at.mLine].size())
	{
		auto& line = mLines[at.mLine];
		isword = isalnum(line[cindex].mChar);
		skip = isword;
	}

	while (!isword || skip)
	{
		if (at.mLine >= mLines.size())
		{
			auto l = std::max(0, (int) mLines.size() - 1);
			return Coordinates(l, GetLineMaxColumn(l));
		}

		auto& line = mLines[at.mLine];
		if (cindex < (int)line.size())
		{
			isword = isalnum(line[cindex].mChar);

			if (isword && !skip)
				return Coordinates(at.mLine, GetCharacterColumn(at.mLine, cindex));

			if (!isword)
				skip = false;

			cindex++;
		}
		else
		{
			cindex = 0;
			++at.mLine;
			skip = false;
			isword = false;
		}
	}

	return at;
}

int TextEditor::GetCharacterIndex(const Coordinates& aCoordinates) const
{
	if (aCoordinates.mLine >= mLines.size())
		return -1;
	auto& line = mLines[aCoordinates.mLine];
	int c = 0;
	int i = 0;
	for (; i < line.size() && c < aCoordinates.mColumn;)
	{
		if (line[i].mChar == '\t')
			c = (c / mTabSize) * mTabSize + mTabSize;
		else
			++c;
		i += UTF8CharLength(line[i].mChar);
	}
	return i;
}

int TextEditor::GetCharacterColumn(int aLine, int aIndex) const
{
	if (aLine >= mLines.size())
		return 0;
	auto& line = mLines[aLine];
	int col = 0;
	int i = 0;
	while (i < aIndex && i < (int)line.size())
	{
		auto c = line[i].mChar;
		i += UTF8CharLength(c);
		if (c == '\t')
			col = (col / mTabSize) * mTabSize + mTabSize;
		else
			col++;
	}
	return col;
}

int TextEditor::GetLineCharacterCount(int aLine) const
{
	if (aLine >= mLines.size())
		return 0;
	auto& line = mLines[aLine];
	int c = 0;
	for (unsigned i = 0; i < line.size(); c++)
		i += UTF8CharLength(line[i].mChar);
	return c;
}

int TextEditor::GetLineMaxColumn(int aLine) const
{
	if (aLine >= mLines.size())
		return 0;
	auto& line = mLines[aLine];
	int col = 0;
	for (unsigned i = 0; i < line.size(); )
	{
		auto c = line[i].mChar;
		if (c == '\t')
			col = (col / mTabSize) * mTabSize + mTabSize;
		else
			col++;
		i += UTF8CharLength(c);
	}
	return col;
}

bool TextEditor::IsOnWordBoundary(const Coordinates & aAt) const
{
	if (aAt.mLine >= (int)mLines.size() || aAt.mColumn == 0)
		return true;

	auto& line = mLines[aAt.mLine];
	auto cindex = GetCharacterIndex(aAt);
	if (cindex >= (int)line.size())
		return true;

	if (mColorizerEnabled)
		return line[cindex].mColorIndex != line[size_t(cindex - 1)].mColorIndex;

	return isspace(line[cindex].mChar) != isspace(line[cindex - 1].mChar);
}

void TextEditor::RemoveLine(int aStart, int aEnd)
{
	assert(!mReadOnly);
	assert(aEnd >= aStart);
	assert(mLines.size() > (size_t)(aEnd - aStart));

	ErrorMarkers etmp;
	for (auto& i : mErrorMarkers)
	{
		ErrorMarkers::value_type e(i.first >= aStart ? i.first - 1 : i.first, i.second);
		if (e.first >= aStart && e.first <= aEnd)
			continue;
		etmp.insert(e);
	}
	mErrorMarkers = std::move(etmp);

	Breakpoints btmp;
	for (auto i : mBreakpoints)
	{
		if (i >= aStart && i <= aEnd)
			continue;
		btmp.insert(i >= aStart ? i - 1 : i);
	}
	mBreakpoints = std::move(btmp);

	mLines.erase(mLines.begin() + aStart, mLines.begin() + aEnd);
	assert(!mLines.empty());

	mTextChanged = true;
}

void TextEditor::RemoveLine(int aIndex)
{
	assert(!mReadOnly);
	assert(mLines.size() > 1);

	ErrorMarkers etmp;
	for (auto& i : mErrorMarkers)
	{
		ErrorMarkers::value_type e(i.first > aIndex ? i.first - 1 : i.first, i.second);
		if (e.first - 1 == aIndex)
			continue;
		etmp.insert(e);
	}
	mErrorMarkers = std::move(etmp);

	Breakpoints btmp;
	for (auto i : mBreakpoints)
	{
		if (i == aIndex)
			continue;
		btmp.insert(i >= aIndex ? i - 1 : i);
	}
	mBreakpoints = std::move(btmp);

	mLines.erase(mLines.begin() + aIndex);
	assert(!mLines.empty());

	mTextChanged = true;
}

TextEditor::Line& TextEditor::InsertLine(int aIndex)
{
	assert(!mReadOnly);

	auto& result = *mLines.insert(mLines.begin() + aIndex, Line());

	ErrorMarkers etmp;
	for (auto& i : mErrorMarkers)
		etmp.insert(ErrorMarkers::value_type(i.first >= aIndex ? i.first + 1 : i.first, i.second));
	mErrorMarkers = std::move(etmp);

	Breakpoints btmp;
	for (auto i : mBreakpoints)
		btmp.insert(i >= aIndex ? i + 1 : i);
	mBreakpoints = std::move(btmp);

	return result;
}

std::string TextEditor::GetWordUnderCursor() const
{
	auto c = GetCursorPosition();
	return GetWordAt(c);
}

std::string TextEditor::GetWordAt(const Coordinates & aCoords) const
{
	auto start = FindWordStart(aCoords);
	auto end = FindWordEnd(aCoords);

	std::string r;

	auto istart = GetCharacterIndex(start);
	auto iend = GetCharacterIndex(end);

	for (auto it = istart; it < iend; ++it)
		r.push_back(mLines[aCoords.mLine][it].mChar);

	return r;
}

ImU32 TextEditor::GetGlyphColor(const Glyph & aGlyph) const
{
	if (!mColorizerEnabled)
		return mPalette[(int)PaletteIndex::Default];
	if (aGlyph.mComment)
		return mPalette[(int)PaletteIndex::Comment];
	if (aGlyph.mMultiLineComment)
		return mPalette[(int)PaletteIndex::MultiLineComment];
	auto const color = mPalette[(int)aGlyph.mColorIndex];
	if (aGlyph.mPreprocessor)
	{
		const auto ppcolor = mPalette[(int)PaletteIndex::Preprocessor];
		const int c0 = ((ppcolor & 0xff) + (color & 0xff)) / 2;
		const int c1 = (((ppcolor >> 8) & 0xff) + ((color >> 8) & 0xff)) / 2;
		const int c2 = (((ppcolor >> 16) & 0xff) + ((color >> 16) & 0xff)) / 2;
		const int c3 = (((ppcolor >> 24) & 0xff) + ((color >> 24) & 0xff)) / 2;
		return ImU32(c0 | (c1 << 8) | (c2 << 16) | (c3 << 24));
	}
	return color;
}

void TextEditor::HandleKeyboardInputs()
{
	ImGuiIO& io = ImGui::GetIO();
	auto shift = io.KeyShift;
	auto ctrl = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
	auto alt = io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeyAlt;

	if (ImGui::IsWindowFocused())
	{
		io.WantCaptureKeyboard = true;
		io.WantTextInput = true;

		if (mAutocompleteOpen)
		{
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
			{
				mAutocompleteSelected = std::min(mAutocompleteSelected + 1, (int)mAutocompleteItems.size() - 1);
				return;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
			{
				mAutocompleteSelected = std::max(0, mAutocompleteSelected - 1);
				return;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Tab))
			{
				if (!mAutocompleteItems.empty())
					ApplyCompletion(mAutocompleteItems[mAutocompleteSelected]);
				return;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Escape))
			{
				CloseAutocomplete();
				return;
			}
		}

		if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Space))
		{
			TriggerAutocomplete();
			return;
		}

		if (ImGui::IsWindowHovered())
			ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
		//ImGui::CaptureKeyboardFromApp(true);

		if (ctrl && !alt && ImGui::IsKeyPressed(ImGuiKey_F))
			OpenFind();
		else if (ctrl && !alt && ImGui::IsKeyPressed(ImGuiKey_H))
		{
			OpenFind();
			mReplaceOpen = true;
		}
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGuiKey_F3))
			FindNext(shift);
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGuiKey_Escape) && (mFindOpen || mGoToLineOpen))
			CloseFind();
		else if (ctrl && !alt && ImGui::IsKeyPressed(ImGuiKey_G))
			OpenGoToLine();
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_M))
			JumpToMatchingBracket();
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Slash))
			ToggleComment();
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_D))
			DuplicateLine();
		else if (alt && !ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_UpArrow))
			MoveLineUp();
		else if (alt && !ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_DownArrow))
			MoveLineDown();
		else if (ctrl && shift && !alt && ImGui::IsKeyPressed(ImGuiKey_K))
			DeleteLine();
		else if (ctrl && shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Enter))
			InsertLineAbove();
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Enter))
			InsertLineBelow();
		else if (!IsReadOnly() && ctrl && shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Z))
			Redo();
		else if (!IsReadOnly() && ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Z))
			Undo();
		else if (!IsReadOnly() && !ctrl && !shift && alt && ImGui::IsKeyPressed(ImGuiKey_Backspace))
			Undo();
		else if (!IsReadOnly() && ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Y))
			Redo();
		else if (!IsReadOnly() && ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Backspace))
		{
			if (!HasSelection())
				MoveLeft(1, true, true);
			Backspace();
		}
		else if (!IsReadOnly() && ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Delete))
		{
			if (!HasSelection())
				MoveRight(1, true, true);
			Delete();
		}
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGuiKey_UpArrow))
			MoveUp(1, shift);
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGuiKey_DownArrow))
			MoveDown(1, shift);
		else if (!alt && ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
			MoveLeft(1, shift, ctrl);
		else if (!alt && ImGui::IsKeyPressed(ImGuiKey_RightArrow))
			MoveRight(1, shift, ctrl);
		else if (!alt && ImGui::IsKeyPressed(ImGuiKey_PageUp))
			MoveUp(GetPageSize() - 4, shift);
		else if (!alt && ImGui::IsKeyPressed(ImGuiKey_PageDown))
			MoveDown(GetPageSize() - 4, shift);
		else if (!alt && ctrl && ImGui::IsKeyPressed(ImGuiKey_Home))
			MoveTop(shift);
		else if (ctrl && !alt && ImGui::IsKeyPressed(ImGuiKey_End))
			MoveBottom(shift);
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGuiKey_Home))
			MoveHome(shift);
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGuiKey_End))
			MoveEnd(shift);
		else if (!IsReadOnly() && !ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Delete))
			Delete();
		else if (!IsReadOnly() && !ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Backspace))
			Backspace();
		else if (!ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Insert))
			mOverwrite ^= true;
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Insert))
			Copy();
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_C))
			Copy();
		else if (!IsReadOnly() && !ctrl && shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Insert))
			Paste();
		else if (!IsReadOnly() && ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_V))
			Paste();
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_X))
			Cut();
		else if (!ctrl && shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Delete))
			Cut();
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_A))
			SelectAll();
		else if (!IsReadOnly() && !ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGuiKey_Enter))
			EnterCharacter('\n', false);
		else if (!IsReadOnly() && !ctrl && !alt && ImGui::IsKeyPressed(ImGuiKey_Tab))
			EnterCharacter('\t', shift);

		bool updateAutocompleteFromInput = false;
		if (!IsReadOnly() && !io.InputQueueCharacters.empty())
		{
			for (int i = 0; i < io.InputQueueCharacters.Size; i++)
			{
				auto c = io.InputQueueCharacters[i];
				if (c != 0 && (c == '\n' || c >= 32))
				{
					const bool handled = HandleAutoClose(c);
					if (!handled)
						EnterCharacter(c, shift);

					if (!handled && (c >= 0x80 || TextEditor_IsIdentifierByte((unsigned char)c)))
						updateAutocompleteFromInput = true;
					else
					{
						CloseAutocomplete();
						updateAutocompleteFromInput = false;
					}
				}
			}
			io.InputQueueCharacters.resize(0);
		}

		if (updateAutocompleteFromInput)
		{
			mAutocompleteOpen = true;
			UpdateAutocomplete();
		}
		else if (mAutocompleteOpen && (mReadOnly || HasSelection()))
		{
			CloseAutocomplete();
		}
		else if (mAutocompleteOpen)
		{
			const auto cursor = GetActualCursorCoordinates();
			if (cursor != mAutocompleteCursor || GetWordPrefixAtCursor() != mAutocompletePrefix || mTextChanged)
				UpdateAutocomplete();
		}
	}
}

void TextEditor::HandleMouseInputs()
{
	ImGuiIO& io = ImGui::GetIO();
	auto shift = io.KeyShift;
	auto ctrl = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
	auto alt = io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeyAlt;

	if (ImGui::IsWindowHovered())
	{
		if (!shift && !alt)
		{
			auto click = ImGui::IsMouseClicked(0);
			auto doubleClick = ImGui::IsMouseDoubleClicked(0);
			auto t = ImGui::GetTime();
			auto tripleClick = click && !doubleClick && (mLastClick != -1.0f && (t - mLastClick) < io.MouseDoubleClickTime);

			/*
			Left mouse button triple click
			*/

			if (tripleClick)
			{
				if (!ctrl)
				{
					mState.mCursorPosition = mInteractiveStart = mInteractiveEnd = ScreenPosToCoordinates(ImGui::GetMousePos());
					mSelectionMode = SelectionMode::Line;
					SetSelection(mInteractiveStart, mInteractiveEnd, mSelectionMode);
				}

				mLastClick = -1.0f;
			}

			/*
			Left mouse button double click
			*/

			else if (doubleClick)
			{
				if (!ctrl)
				{
					mState.mCursorPosition = mInteractiveStart = mInteractiveEnd = ScreenPosToCoordinates(ImGui::GetMousePos());
					if (mSelectionMode == SelectionMode::Line)
						mSelectionMode = SelectionMode::Normal;
					else
						mSelectionMode = SelectionMode::Word;
					SetSelection(mInteractiveStart, mInteractiveEnd, mSelectionMode);
				}

				mLastClick = (float)ImGui::GetTime();
			}

			/*
			Left mouse button click
			*/
			else if (click)
			{
				mState.mCursorPosition = mInteractiveStart = mInteractiveEnd = ScreenPosToCoordinates(ImGui::GetMousePos());
				if (ctrl)
					mSelectionMode = SelectionMode::Word;
				else
					mSelectionMode = SelectionMode::Normal;
				SetSelection(mInteractiveStart, mInteractiveEnd, mSelectionMode);

				mLastClick = (float)ImGui::GetTime();
			}
			// Mouse left button dragging (=> update selection)
			else if (ImGui::IsMouseDragging(0) && ImGui::IsMouseDown(0))
			{
				io.WantCaptureMouse = true;
				mState.mCursorPosition = mInteractiveEnd = ScreenPosToCoordinates(ImGui::GetMousePos());
				SetSelection(mInteractiveStart, mInteractiveEnd, mSelectionMode);
			}
		}
	}
}

void TextEditor::Render()
{
	/* Compute mCharAdvance regarding to scaled font size (Ctrl + mouse wheel)*/
	const float fontSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, "#", nullptr, nullptr).x;
	mCharAdvance = ImVec2(fontSize, ImGui::GetTextLineHeightWithSpacing() * mLineSpacing);

	/* Update palette with the current alpha from style */
	for (int i = 0; i < (int)PaletteIndex::Max; ++i)
	{
		auto color = ImGui::ColorConvertU32ToFloat4(mPaletteBase[i]);
		color.w *= ImGui::GetStyle().Alpha;
		mPalette[i] = ImGui::ColorConvertFloat4ToU32(color);
	}

	assert(mLineBuffer.empty());

	auto contentSize = ImGui::GetWindowContentRegionMax();
	auto drawList = ImGui::GetWindowDrawList();
	float longest(mTextStart);

	if (mScrollToTop)
	{
		mScrollToTop = false;
		ImGui::SetScrollY(0.f);
	}

	ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();
	auto scrollX = ImGui::GetScrollX();
	auto scrollY = ImGui::GetScrollY();

	auto lineNo = (int)floor(scrollY / mCharAdvance.y);
	auto globalLineMax = (int)mLines.size();
	auto lineMax = std::max(0, std::min((int)mLines.size() - 1, lineNo + (int)floor((scrollY + contentSize.y) / mCharAdvance.y)));

	// Deduce mTextStart by evaluating mLines size (global lineMax) plus two spaces as text width
	char buf[16];
	snprintf(buf, 16, " %d ", globalLineMax);
	mTextStart = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, buf, nullptr, nullptr).x + mLeftMargin;

	if (!mLines.empty())
	{
		float spaceSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, " ", nullptr, nullptr).x;

		while (lineNo <= lineMax)
		{
			ImVec2 lineStartScreenPos = ImVec2(cursorScreenPos.x, cursorScreenPos.y + lineNo * mCharAdvance.y);
			ImVec2 textScreenPos = ImVec2(lineStartScreenPos.x + mTextStart, lineStartScreenPos.y);

			auto& line = mLines[lineNo];
			longest = std::max(mTextStart + TextDistanceToLineStart(Coordinates(lineNo, GetLineMaxColumn(lineNo))), longest);
			auto columnNo = 0;
			Coordinates lineStartCoord(lineNo, 0);
			Coordinates lineEndCoord(lineNo, GetLineMaxColumn(lineNo));

			// Draw selection for the current line
			float sstart = -1.0f;
			float ssend = -1.0f;

			assert(mState.mSelectionStart <= mState.mSelectionEnd);
			if (mState.mSelectionStart <= lineEndCoord)
				sstart = mState.mSelectionStart > lineStartCoord ? TextDistanceToLineStart(mState.mSelectionStart) : 0.0f;
			if (mState.mSelectionEnd > lineStartCoord)
				ssend = TextDistanceToLineStart(mState.mSelectionEnd < lineEndCoord ? mState.mSelectionEnd : lineEndCoord);

			if (mState.mSelectionEnd.mLine > lineNo)
				ssend += mCharAdvance.x;

			if (sstart != -1 && ssend != -1 && sstart < ssend)
			{
				ImVec2 vstart(lineStartScreenPos.x + mTextStart + sstart, lineStartScreenPos.y);
				ImVec2 vend(lineStartScreenPos.x + mTextStart + ssend, lineStartScreenPos.y + mCharAdvance.y);
				drawList->AddRectFilled(vstart, vend, mPalette[(int)PaletteIndex::Selection]);
			}

			// Draw breakpoints
			auto start = ImVec2(lineStartScreenPos.x + scrollX, lineStartScreenPos.y);

			if (mBreakpoints.count(lineNo + 1) != 0)
			{
				auto end = ImVec2(lineStartScreenPos.x + contentSize.x + 2.0f * scrollX, lineStartScreenPos.y + mCharAdvance.y);
				drawList->AddRectFilled(start, end, mPalette[(int)PaletteIndex::Breakpoint]);
			}

			// Draw error markers
			auto errorIt = mErrorMarkers.find(lineNo + 1);
			if (errorIt != mErrorMarkers.end())
			{
				auto end = ImVec2(lineStartScreenPos.x + contentSize.x + 2.0f * scrollX, lineStartScreenPos.y + mCharAdvance.y);
				drawList->AddRectFilled(start, end, mPalette[(int)PaletteIndex::ErrorMarker]);

				if (ImGui::IsMouseHoveringRect(lineStartScreenPos, end))
				{
					ImGui::BeginTooltip();
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
					ImGui::Text("Error at line %d:", errorIt->first);
					ImGui::PopStyleColor();
					ImGui::Separator();
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.2f, 1.0f));
					ImGui::Text("%s", errorIt->second.c_str());
					ImGui::PopStyleColor();
					ImGui::EndTooltip();
				}
			}

			// Draw line number (right aligned)
			snprintf(buf, 16, "%d  ", lineNo + 1);

			auto lineNoWidth = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, buf, nullptr, nullptr).x;
			drawList->AddText(ImVec2(lineStartScreenPos.x + mTextStart - lineNoWidth, lineStartScreenPos.y), mPalette[(int)PaletteIndex::LineNumber], buf);

			if (mState.mCursorPosition.mLine == lineNo)
			{
				auto focused = ImGui::IsWindowFocused();

				// Highlight the current line (where the cursor is)
				if (!HasSelection())
				{
					auto end = ImVec2(start.x + contentSize.x + scrollX, start.y + mCharAdvance.y);
					drawList->AddRectFilled(start, end, mPalette[(int)(focused ? PaletteIndex::CurrentLineFill : PaletteIndex::CurrentLineFillInactive)]);
					drawList->AddRect(start, end, mPalette[(int)PaletteIndex::CurrentLineEdge], 1.0f);
				}

				// Render the cursor
				if (focused)
				{
					auto timeEnd = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
					auto elapsed = timeEnd - mStartTime;
					if (elapsed > 400)
					{
						float width = 1.0f;
						auto cindex = GetCharacterIndex(mState.mCursorPosition);
						float cx = TextDistanceToLineStart(mState.mCursorPosition);

						if (mOverwrite && cindex < (int)line.size())
						{
							auto c = line[cindex].mChar;
							if (c == '\t')
							{
								auto x = (1.0f + std::floor((1.0f + cx) / (float(mTabSize) * spaceSize))) * (float(mTabSize) * spaceSize);
								width = x - cx;
							}
							else
							{
								char buf2[2];
								buf2[0] = line[cindex].mChar;
								buf2[1] = '\0';
								width = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, buf2).x;
							}
						}
						ImVec2 cstart(textScreenPos.x + cx, lineStartScreenPos.y);
						ImVec2 cend(textScreenPos.x + cx + width, lineStartScreenPos.y + mCharAdvance.y);
						drawList->AddRectFilled(cstart, cend, mPalette[(int)PaletteIndex::Cursor]);
						if (elapsed > 800)
							mStartTime = timeEnd;
					}
				}
			}

			// Render colorized text
			auto prevColor = line.empty() ? mPalette[(int)PaletteIndex::Default] : GetGlyphColor(line[0]);
			ImVec2 bufferOffset;

			for (int i = 0; i < line.size();)
			{
				auto& glyph = line[i];
				auto color = GetGlyphColor(glyph);

				if ((color != prevColor || glyph.mChar == '\t' || glyph.mChar == ' ') && !mLineBuffer.empty())
				{
					const ImVec2 newOffset(textScreenPos.x + bufferOffset.x, textScreenPos.y + bufferOffset.y);
					drawList->AddText(newOffset, prevColor, mLineBuffer.c_str());
					auto textSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, mLineBuffer.c_str(), nullptr, nullptr);
					bufferOffset.x += textSize.x;
					mLineBuffer.clear();
				}
				prevColor = color;

				if (glyph.mChar == '\t')
				{
					auto oldX = bufferOffset.x;
					bufferOffset.x = (1.0f + std::floor((1.0f + bufferOffset.x) / (float(mTabSize) * spaceSize))) * (float(mTabSize) * spaceSize);
					++i;

					if (mShowWhitespaces)
					{
						const auto s = ImGui::GetFontSize();
						const auto x1 = textScreenPos.x + oldX + 1.0f;
						const auto x2 = textScreenPos.x + bufferOffset.x - 1.0f;
						const auto y = textScreenPos.y + bufferOffset.y + s * 0.5f;
						const ImVec2 p1(x1, y);
						const ImVec2 p2(x2, y);
						const ImVec2 p3(x2 - s * 0.2f, y - s * 0.2f);
						const ImVec2 p4(x2 - s * 0.2f, y + s * 0.2f);
						drawList->AddLine(p1, p2, 0x90909090);
						drawList->AddLine(p2, p3, 0x90909090);
						drawList->AddLine(p2, p4, 0x90909090);
					}
				}
				else if (glyph.mChar == ' ')
				{
					if (mShowWhitespaces)
					{
						const auto s = ImGui::GetFontSize();
						const auto x = textScreenPos.x + bufferOffset.x + spaceSize * 0.5f;
						const auto y = textScreenPos.y + bufferOffset.y + s * 0.5f;
						drawList->AddCircleFilled(ImVec2(x, y), 1.5f, 0x80808080, 4);
					}
					bufferOffset.x += spaceSize;
					i++;
				}
				else
				{
					auto l = UTF8CharLength(glyph.mChar);
					while (l-- > 0)
						mLineBuffer.push_back(line[i++].mChar);
				}
				++columnNo;
			}

			if (!mLineBuffer.empty())
			{
				const ImVec2 newOffset(textScreenPos.x + bufferOffset.x, textScreenPos.y + bufferOffset.y);
				drawList->AddText(newOffset, prevColor, mLineBuffer.c_str());
				mLineBuffer.clear();
			}

			++lineNo;
		}

		// Draw a tooltip on known identifiers/preprocessor symbols
		if (ImGui::IsMousePosValid())
		{
			auto id = GetWordAt(ScreenPosToCoordinates(ImGui::GetMousePos()));
			if (!id.empty())
			{
				auto it = mLanguageDefinition.mIdentifiers.find(id);
				if (it != mLanguageDefinition.mIdentifiers.end())
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted(it->second.mDeclaration.c_str());
					ImGui::EndTooltip();
				}
				else
				{
					auto pi = mLanguageDefinition.mPreprocIdentifiers.find(id);
					if (pi != mLanguageDefinition.mPreprocIdentifiers.end())
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted(pi->second.mDeclaration.c_str());
						ImGui::EndTooltip();
					}
				}
			}
		}
	}

	RenderFindHighlights();
	RenderOccurrences();
	RenderBracketHighlight();
	RenderAutocomplete();


	ImGui::Dummy(ImVec2((longest + 2), mLines.size() * mCharAdvance.y));

	if (mScrollToCursor)
	{
		EnsureCursorVisible();
		ImGui::SetWindowFocus();
		mScrollToCursor = false;
	}
}

void TextEditor::Render(const char* aTitle, const ImVec2& aSize, bool aBorder)
{
	mWithinRender = true;
	mTextChanged = false;
	mCursorPositionChanged = false;

	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(mPalette[(int)PaletteIndex::Background]));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	if (!mIgnoreImGuiChild)
		ImGui::BeginChild(aTitle, aSize, aBorder, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar | ImGuiWindowFlags_NoMove);

	if (mHandleKeyboardInputs)
	{
		HandleKeyboardInputs();
		ImGui::PushAllowKeyboardFocus(true);
	}

	if (mHandleMouseInputs)
		HandleMouseInputs();

	ColorizeInternal();
	Render();

	if (mHandleKeyboardInputs)
		ImGui::PopAllowKeyboardFocus();

	if (!mIgnoreImGuiChild)
		ImGui::EndChild();

	ImGui::PopStyleVar();
	ImGui::PopStyleColor();

	mWithinRender = false;
	RenderFindReplace();
}

void TextEditor::OpenFind()
{
	mFindOpen = true;
	mReplaceOpen = false;
	mFindFocusRequested = true;
	UpdateFindMatches();
	if (mFindCurrentMatch >= 0)
		SelectFindMatch(mFindCurrentMatch);
}

void TextEditor::CloseFind()
{
	mFindOpen = false;
	mReplaceOpen = false;
	mGoToLineOpen = false;
	mFindMatchStarts.clear();
	mFindMatchEnds.clear();
	mFindCurrentMatch = -1;
	mFindError.clear();
}

void TextEditor::UpdateFindMatches()
{
	mFindMatchStarts.clear();
	mFindMatchEnds.clear();
	mFindCurrentMatch = -1;
	mFindError.clear();

	std::string needle(mFindBuffer);
	if (needle.empty())
		return;

	auto lowercase = [](std::string& value)
	{
		for (auto& character : value)
			character = (char)std::tolower((unsigned char)character);
	};
	if (!mFindCaseSensitive && !mFindRegex)
		lowercase(needle);

	std::regex expression;
	if (mFindRegex)
	{
		auto flags = std::regex_constants::ECMAScript | std::regex_constants::optimize;
		if (!mFindCaseSensitive)
			flags |= std::regex_constants::icase;
		try
		{
			expression.assign(mFindBuffer, flags);
		}
		catch (const std::regex_error& error)
		{
			mFindError = error.what();
			return;
		}
	}

	auto isWordByte = [](unsigned char character)
	{
		return std::isalnum(character) != 0 || character == '_';
	};
	for (int lineIndex = 0; lineIndex < (int)mLines.size(); ++lineIndex)
	{
		const auto& line = mLines[lineIndex];
		std::string text;
		text.reserve(line.size());
		for (const auto& glyph : line)
			text.push_back((char)glyph.mChar);

		std::string haystack = text;
		if (!mFindCaseSensitive && !mFindRegex)
			lowercase(haystack);

		auto addMatch = [&](size_t position, size_t length)
		{
			if (length == 0 || position + length > text.size())
				return;
			auto isBoundary = [&](size_t index)
			{
				return index == 0 || index == text.size() ||
					((unsigned char)text[index] & 0xC0) != 0x80;
			};
			if (!isBoundary(position) || !isBoundary(position + length))
				return;
			if (mFindWholeWord &&
				((position > 0 && isWordByte((unsigned char)text[position - 1])) ||
				(position + length < text.size() && isWordByte((unsigned char)text[position + length]))))
				return;

			mFindMatchStarts.push_back(Coordinates(lineIndex, GetCharacterColumn(lineIndex, (int)position)));
			mFindMatchEnds.push_back(Coordinates(lineIndex, GetCharacterColumn(lineIndex, (int)(position + length))));
		};

		if (mFindRegex)
		{
			for (std::sregex_iterator it(text.begin(), text.end(), expression), end; it != end; ++it)
				addMatch((size_t)it->position(), (size_t)it->length());
		}
		else
		{
			size_t position = 0;
			while ((position = haystack.find(needle, position)) != std::string::npos)
			{
				addMatch(position, needle.size());
				position += needle.size();
			}
		}
	}

	const auto cursor = GetActualCursorCoordinates();
	for (int i = 0; i < (int)mFindMatchStarts.size(); ++i)
	{
		if (mFindMatchStarts[i] >= cursor)
		{
			mFindCurrentMatch = i;
			break;
		}
	}
	if (mFindCurrentMatch < 0 && !mFindMatchStarts.empty())
		mFindCurrentMatch = 0;
}

void TextEditor::SelectFindMatch(int aIndex)
{
	if (aIndex < 0 || aIndex >= (int)mFindMatchStarts.size())
		return;
	mFindCurrentMatch = aIndex;
	SetSelection(mFindMatchStarts[aIndex], mFindMatchEnds[aIndex]);
	SetCursorPosition(mFindMatchStarts[aIndex]);
	mInteractiveStart = mFindMatchStarts[aIndex];
	mInteractiveEnd = mFindMatchEnds[aIndex];
	EnsureCursorVisible();
}

void TextEditor::FindNext(bool aBackwards)
{
	if (mFindMatchStarts.empty())
		UpdateFindMatches();
	if (mFindMatchStarts.empty())
		return;

	const int count = (int)mFindMatchStarts.size();
	int index = mFindCurrentMatch;
	if (index < 0)
		index = aBackwards ? count - 1 : 0;
	else
		index = aBackwards ? (index + count - 1) % count : (index + 1) % count;
	SelectFindMatch(index);
}

void TextEditor::ReplaceOne()
{
	if (mReadOnly)
		return;
	if (mFindCurrentMatch < 0 || mFindCurrentMatch >= (int)mFindMatchStarts.size())
		FindNext(false);
	if (mFindCurrentMatch < 0)
		return;

	UndoRecord undo;
	undo.mBefore = mState;
	undo.mRemovedStart = mFindMatchStarts[mFindCurrentMatch];
	undo.mRemovedEnd = mFindMatchEnds[mFindCurrentMatch];
	undo.mRemoved = GetText(undo.mRemovedStart, undo.mRemovedEnd);

	DeleteRange(undo.mRemovedStart, undo.mRemovedEnd);
	Coordinates end = undo.mRemovedStart;
	InsertTextAt(end, mReplaceBuffer);
	undo.mAddedStart = undo.mRemovedStart;
	undo.mAddedEnd = end;
	undo.mAdded = GetText(undo.mAddedStart, undo.mAddedEnd);

	SetCursorPosition(end);
	SetSelection(end, end);
	mInteractiveStart = mInteractiveEnd = end;
	undo.mAfter = mState;
	AddUndo(undo);
	Colorize(undo.mRemovedStart.mLine, 1);
	EnsureCursorVisible();

	UpdateFindMatches();
	if (mFindCurrentMatch >= 0)
		SelectFindMatch(mFindCurrentMatch);
}

void TextEditor::ReplaceAll()
{
	if (mReadOnly)
		return;
	UpdateFindMatches();
	if (mFindMatchStarts.empty())
		return;

	const auto starts = mFindMatchStarts;
	const auto ends = mFindMatchEnds;
	const int lastLine = ends.back().mLine;
	const int oldEndIndex = GetCharacterIndex(ends.back());
	int byteDeltaOnLastLine = 0;
	for (size_t i = 0; i < starts.size(); ++i)
	{
		if (starts[i].mLine == lastLine)
			byteDeltaOnLastLine += (int)std::strlen(mReplaceBuffer) -
				(GetCharacterIndex(ends[i]) - GetCharacterIndex(starts[i]));
	}

	UndoRecord undo;
	undo.mBefore = mState;
	undo.mRemovedStart = starts.front();
	undo.mRemovedEnd = ends.back();
	undo.mRemoved = GetText(undo.mRemovedStart, undo.mRemovedEnd);

	for (int i = (int)starts.size() - 1; i >= 0; --i)
	{
		DeleteRange(starts[i], ends[i]);
		auto end = starts[i];
		InsertTextAt(end, mReplaceBuffer);
	}

	const int newEndIndex = oldEndIndex + byteDeltaOnLastLine;
	undo.mAddedStart = undo.mRemovedStart;
	undo.mAddedEnd = Coordinates(lastLine, GetCharacterColumn(lastLine, newEndIndex));
	undo.mAdded = GetText(undo.mAddedStart, undo.mAddedEnd);
	SetCursorPosition(undo.mAddedEnd);
	SetSelection(undo.mAddedEnd, undo.mAddedEnd);
	mInteractiveStart = mInteractiveEnd = undo.mAddedEnd;
	undo.mAfter = mState;
	AddUndo(undo);
	Colorize(undo.mRemovedStart.mLine, lastLine - undo.mRemovedStart.mLine + 1);
	EnsureCursorVisible();

	UpdateFindMatches();
}

void TextEditor::RenderFindReplace()
{
	if (!mFindOpen && !mGoToLineOpen)
		return;

	ImGui::PushID(this);
	if (mFindOpen)
	{
		ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);
		const bool visible = ImGui::Begin("Find / Replace", &mFindOpen, ImGuiWindowFlags_NoCollapse);
		if (visible)
		{
			if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape))
				CloseFind();
			else if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_F3))
				FindNext(ImGui::GetIO().KeyShift);

			if (mFindFocusRequested)
			{
				ImGui::SetKeyboardFocusHere();
				mFindFocusRequested = false;
			}

			bool changed = ImGui::InputTextWithHint("##find", "Find...", mFindBuffer, sizeof(mFindBuffer));
			if (mReplaceOpen)
				ImGui::InputTextWithHint("##replace", "Replace...", mReplaceBuffer, sizeof(mReplaceBuffer));

			changed |= ImGui::Checkbox("Case sensitive", &mFindCaseSensitive);
			ImGui::SameLine();
			changed |= ImGui::Checkbox("Whole word", &mFindWholeWord);
			ImGui::SameLine();
			changed |= ImGui::Checkbox("Regex", &mFindRegex);
			if (changed)
			{
				UpdateFindMatches();
				if (mFindCurrentMatch >= 0)
					SelectFindMatch(mFindCurrentMatch);
			}

			if (ImGui::Button("Find Next"))
				FindNext(false);
			ImGui::SameLine();
			if (ImGui::Button("Find Prev"))
				FindNext(true);
			if (mReplaceOpen)
			{
				ImGui::SameLine();
				if (ImGui::Button("Replace"))
					ReplaceOne();
				ImGui::SameLine();
				if (ImGui::Button("Replace All"))
					ReplaceAll();
			}
			ImGui::SameLine();
			if (ImGui::Button("Close"))
				CloseFind();

			if (!mFindError.empty())
				ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", mFindError.c_str());
			else if (!mFindMatchStarts.empty())
				ImGui::Text("%d/%d", mFindCurrentMatch + 1, (int)mFindMatchStarts.size());
			else
				ImGui::TextUnformatted(mFindBuffer[0] ? "No matches" : "");
		}
		ImGui::End();
		if (!mFindOpen)
			CloseFind();
		else if (mTextChanged)
		{
			UpdateFindMatches();
		}
	}

	if (mGoToLineOpen)
	{
		ImGui::SetNextWindowSize(ImVec2(240.0f, 0.0f), ImGuiCond_Appearing);
		const bool visible = ImGui::Begin("Go to Line", &mGoToLineOpen, ImGuiWindowFlags_NoCollapse);
		if (visible)
		{
			if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape))
				mGoToLineOpen = false;
			if (mGoToLineFocusRequested)
			{
				ImGui::SetKeyboardFocusHere();
				mGoToLineFocusRequested = false;
			}
			const bool enter = ImGui::InputInt("Line##go", &mGoToLineValue, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue);
			if (ImGui::Button("Go") || enter)
			{
				const int line = std::max(0, std::min((int)mLines.size() - 1, mGoToLineValue - 1));
				const Coordinates position(line, 0);
				SetCursorPosition(position);
				SetSelection(position, position);
				mInteractiveStart = mInteractiveEnd = position;
				EnsureCursorVisible();
				mGoToLineOpen = false;
			}
		}
		ImGui::End();
	}
	ImGui::PopID();
}

void TextEditor::OpenGoToLine()
{
	mGoToLineOpen = true;
	mGoToLineFocusRequested = true;
	mGoToLineValue = mState.mCursorPosition.mLine + 1;
}

void TextEditor::GetSelectedLineRange(int& aStart, int& aEnd) const
{
	if (HasSelection())
	{
		aStart = mState.mSelectionStart.mLine;
		aEnd = mState.mSelectionEnd.mLine;
		if (aEnd > aStart && mState.mSelectionEnd.mColumn == 0)
			--aEnd;
	}
	else
		aStart = aEnd = mState.mCursorPosition.mLine;

	aStart = std::max(0, std::min((int)mLines.size() - 1, aStart));
	aEnd = std::max(aStart, std::min((int)mLines.size() - 1, aEnd));
}

void TextEditor::ToggleComment()
{
	if (mReadOnly || mLines.empty())
		return;

	const auto& comment = mLanguageDefinition.mSingleLineComment;
	if (comment.empty())
		return;

	int firstLine, lastLine;
	GetSelectedLineRange(firstLine, lastLine);
	const int lineCount = lastLine - firstLine + 1;
	std::vector<int> commentColumns((size_t)lineCount);
	std::vector<int> commentWidths((size_t)lineCount);
	bool allCommented = true;
	for (int lineIndex = firstLine; lineIndex <= lastLine; ++lineIndex)
	{
		const auto& line = mLines[lineIndex];
		size_t index = 0;
		while (index < line.size() && (line[index].mChar == ' ' || line[index].mChar == '\t'))
			++index;
		commentColumns[(size_t)(lineIndex - firstLine)] = GetCharacterColumn(lineIndex, (int)index);

		bool commented = index + comment.size() <= line.size();
		for (size_t i = 0; commented && i < comment.size(); ++i)
			commented = line[index + i].mChar == (Char)comment[i];
		if (!commented)
			allCommented = false;
	}

	UndoRecord undo;
	undo.mBefore = mState;
	undo.mRemovedStart = Coordinates(firstLine, 0);
	undo.mRemovedEnd = Coordinates(lastLine, GetLineMaxColumn(lastLine));
	undo.mRemoved = GetText(undo.mRemovedStart, undo.mRemovedEnd);

	for (int lineIndex = firstLine; lineIndex <= lastLine; ++lineIndex)
	{
		auto& line = mLines[lineIndex];
		size_t index = 0;
		while (index < line.size() && (line[index].mChar == ' ' || line[index].mChar == '\t'))
			++index;

		if (allCommented)
		{
			size_t removeCount = comment.size();
			if (index + removeCount < line.size() && line[index + removeCount].mChar == ' ')
				++removeCount;
			const int before = GetCharacterColumn(lineIndex, (int)index);
			const int after = GetCharacterColumn(lineIndex, (int)(index + removeCount));
			commentWidths[(size_t)(lineIndex - firstLine)] = after - before;
			line.erase(line.begin() + index, line.begin() + index + removeCount);
		}
		else
		{
			for (size_t i = 0; i < comment.size(); ++i)
				line.insert(line.begin() + index + i, Glyph((Char)comment[i], PaletteIndex::Comment));
			const int after = GetCharacterColumn(lineIndex, (int)(index + comment.size()));
			commentWidths[(size_t)(lineIndex - firstLine)] =
				after - commentColumns[(size_t)(lineIndex - firstLine)];
		}
	}

	auto adjustPosition = [&](Coordinates& position)
	{
		if (position.mLine < firstLine || position.mLine > lastLine)
			return;
		const size_t i = (size_t)(position.mLine - firstLine);
		const int column = commentColumns[i];
		const int width = commentWidths[i];
		if (allCommented)
		{
			if (position.mColumn > column)
				position.mColumn = std::max(column, position.mColumn - width);
		}
		else if (position.mColumn >= column)
			position.mColumn += width;
	};
	adjustPosition(mState.mCursorPosition);
	adjustPosition(mState.mSelectionStart);
	adjustPosition(mState.mSelectionEnd);
	adjustPosition(mInteractiveStart);
	adjustPosition(mInteractiveEnd);

	undo.mAddedStart = undo.mRemovedStart;
	undo.mAddedEnd = Coordinates(lastLine, GetLineMaxColumn(lastLine));
	undo.mAdded = GetText(undo.mAddedStart, undo.mAddedEnd);
	undo.mAfter = mState;
	AddUndo(undo);
	mTextChanged = true;
	Colorize(firstLine, lineCount);
}

void TextEditor::DuplicateLine()
{
	if (mReadOnly || mLines.empty())
		return;

	int firstLine, lastLine;
	GetSelectedLineRange(firstLine, lastLine);
	Lines copies(mLines.begin() + firstLine, mLines.begin() + lastLine + 1);
	const int count = (int)copies.size();

	UndoRecord undo;
	undo.mBefore = mState;
	undo.mAddedStart = Coordinates(lastLine, GetLineMaxColumn(lastLine));
	for (int i = 0; i < count; ++i)
	{
		InsertLine(lastLine + 1 + i);
		mLines[lastLine + 1 + i] = std::move(copies[(size_t)i]);
	}
	undo.mAddedEnd = Coordinates(lastLine + count, GetLineMaxColumn(lastLine + count));
	undo.mAdded = GetText(undo.mAddedStart, undo.mAddedEnd);
	undo.mAfter = mState;
	AddUndo(undo);
	mTextChanged = true;
	Colorize(lastLine, count + 1);
}

void TextEditor::MoveLineUp()
{
	if (mReadOnly || mLines.empty())
		return;
	int firstLine, lastLine;
	GetSelectedLineRange(firstLine, lastLine);
	if (firstLine == 0)
		return;
	const bool hadSelection = HasSelection();

	UndoRecord undo;
	undo.mBefore = mState;
	undo.mRemovedStart = Coordinates(firstLine - 1, 0);
	undo.mRemovedEnd = Coordinates(lastLine, GetLineMaxColumn(lastLine));
	undo.mRemoved = GetText(undo.mRemovedStart, undo.mRemovedEnd);

	std::rotate(mLines.begin() + firstLine - 1, mLines.begin() + firstLine, mLines.begin() + lastLine + 1);
	mCursorPositionChanged = true;
	ErrorMarkers movedErrors;
	for (const auto& marker : mErrorMarkers)
	{
		int line = marker.first;
		if (line == firstLine)
			line = lastLine + 1;
		else if (line > firstLine && line <= lastLine + 1)
			--line;
		movedErrors.insert(ErrorMarkers::value_type(line, marker.second));
	}
	mErrorMarkers = std::move(movedErrors);
	Breakpoints movedBreakpoints;
	for (int line : mBreakpoints)
	{
		if (line == firstLine)
			line = lastLine + 1;
		else if (line > firstLine && line <= lastLine + 1)
			--line;
		movedBreakpoints.insert(line);
	}
	mBreakpoints = std::move(movedBreakpoints);
	--mState.mCursorPosition.mLine;
	if (hadSelection)
	{
		--mState.mSelectionStart.mLine;
		--mState.mSelectionEnd.mLine;
		mInteractiveStart = mState.mSelectionStart;
		mInteractiveEnd = mState.mSelectionEnd;
	}
	else
	{
		mState.mSelectionStart = mState.mSelectionEnd = mState.mCursorPosition;
		mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
	}

	undo.mAddedStart = undo.mRemovedStart;
	undo.mAddedEnd = Coordinates(lastLine, GetLineMaxColumn(lastLine));
	undo.mAdded = GetText(undo.mAddedStart, undo.mAddedEnd);
	undo.mAfter = mState;
	AddUndo(undo);
	mTextChanged = true;
	Colorize(firstLine - 1, lastLine - firstLine + 2);
	EnsureCursorVisible();
}

void TextEditor::MoveLineDown()
{
	if (mReadOnly || mLines.empty())
		return;
	int firstLine, lastLine;
	GetSelectedLineRange(firstLine, lastLine);
	if (lastLine >= (int)mLines.size() - 1)
		return;
	const bool hadSelection = HasSelection();

	UndoRecord undo;
	undo.mBefore = mState;
	undo.mRemovedStart = Coordinates(firstLine, 0);
	undo.mRemovedEnd = Coordinates(lastLine + 1, GetLineMaxColumn(lastLine + 1));
	undo.mRemoved = GetText(undo.mRemovedStart, undo.mRemovedEnd);

	std::rotate(mLines.begin() + firstLine, mLines.begin() + lastLine + 1, mLines.begin() + lastLine + 2);
	mCursorPositionChanged = true;
	ErrorMarkers movedErrors;
	for (const auto& marker : mErrorMarkers)
	{
		int line = marker.first;
		if (line == lastLine + 2)
			line = firstLine + 1;
		else if (line >= firstLine + 1 && line <= lastLine + 1)
			++line;
		movedErrors.insert(ErrorMarkers::value_type(line, marker.second));
	}
	mErrorMarkers = std::move(movedErrors);
	Breakpoints movedBreakpoints;
	for (int line : mBreakpoints)
	{
		if (line == lastLine + 2)
			line = firstLine + 1;
		else if (line >= firstLine + 1 && line <= lastLine + 1)
			++line;
		movedBreakpoints.insert(line);
	}
	mBreakpoints = std::move(movedBreakpoints);
	++mState.mCursorPosition.mLine;
	if (hadSelection)
	{
		++mState.mSelectionStart.mLine;
		++mState.mSelectionEnd.mLine;
		mInteractiveStart = mState.mSelectionStart;
		mInteractiveEnd = mState.mSelectionEnd;
	}
	else
	{
		mState.mSelectionStart = mState.mSelectionEnd = mState.mCursorPosition;
		mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
	}

	undo.mAddedStart = undo.mRemovedStart;
	undo.mAddedEnd = Coordinates(lastLine + 1, GetLineMaxColumn(lastLine + 1));
	undo.mAdded = GetText(undo.mAddedStart, undo.mAddedEnd);
	undo.mAfter = mState;
	AddUndo(undo);
	mTextChanged = true;
	Colorize(firstLine, lastLine - firstLine + 2);
	EnsureCursorVisible();
}

void TextEditor::DeleteLine()
{
	if (mReadOnly || mLines.empty())
		return;

	int firstLine, lastLine;
	GetSelectedLineRange(firstLine, lastLine);
	UndoRecord undo;
	undo.mBefore = mState;

	if (firstLine == 0 && lastLine == (int)mLines.size() - 1)
	{
		undo.mRemovedStart = Coordinates(0, 0);
		undo.mRemovedEnd = Coordinates(lastLine, GetLineMaxColumn(lastLine));
	}
	else if (lastLine < (int)mLines.size() - 1)
	{
		undo.mRemovedStart = Coordinates(firstLine, 0);
		undo.mRemovedEnd = Coordinates(lastLine + 1, 0);
	}
	else
	{
		undo.mRemovedStart = Coordinates(firstLine - 1, GetLineMaxColumn(firstLine - 1));
		undo.mRemovedEnd = Coordinates(lastLine, GetLineMaxColumn(lastLine));
	}
	undo.mRemoved = GetText(undo.mRemovedStart, undo.mRemovedEnd);
	if (undo.mRemoved.empty())
		return;

	DeleteRange(undo.mRemovedStart, undo.mRemovedEnd);
	Coordinates cursor = undo.mRemovedStart;
	cursor = SanitizeCoordinates(cursor);
	SetCursorPosition(cursor);
	SetSelection(cursor, cursor);
	mInteractiveStart = mInteractiveEnd = cursor;
	undo.mAfter = mState;
	AddUndo(undo);
	Colorize(cursor.mLine, 1);
}

void TextEditor::InsertLineAbove()
{
	if (mReadOnly || mLines.empty())
		return;

	const int line = mState.mCursorPosition.mLine;
	Line indentation;
	if (mLanguageDefinition.mAutoIndentation)
	{
		const auto& current = mLines[line];
		for (const auto& glyph : current)
		{
			if (glyph.mChar != ' ' && glyph.mChar != '\t')
				break;
			indentation.push_back(glyph);
		}
	}

	UndoRecord undo;
	undo.mBefore = mState;
	InsertLine(line);
	mLines[line] = indentation;
	undo.mAddedStart = Coordinates(line, 0);
	undo.mAddedEnd = Coordinates(line + 1, 0);
	undo.mAdded = GetText(undo.mAddedStart, undo.mAddedEnd);
	const Coordinates cursor(line, GetLineMaxColumn(line));
	SetCursorPosition(cursor);
	SetSelection(cursor, cursor);
	mInteractiveStart = mInteractiveEnd = cursor;
	undo.mAfter = mState;
	AddUndo(undo);
	Colorize(line, 2);
}

void TextEditor::InsertLineBelow()
{
	if (mReadOnly || mLines.empty())
		return;

	const int line = mState.mCursorPosition.mLine;
	Line indentation;
	if (mLanguageDefinition.mAutoIndentation)
	{
		const auto& current = mLines[line];
		for (const auto& glyph : current)
		{
			if (glyph.mChar != ' ' && glyph.mChar != '\t')
				break;
			indentation.push_back(glyph);
		}
	}

	UndoRecord undo;
	undo.mBefore = mState;
	undo.mAddedStart = Coordinates(line, GetLineMaxColumn(line));
	InsertLine(line + 1);
	mLines[line + 1] = indentation;
	undo.mAddedEnd = Coordinates(line + 1, GetLineMaxColumn(line + 1));
	undo.mAdded = GetText(undo.mAddedStart, undo.mAddedEnd);
	const Coordinates cursor(line + 1, GetLineMaxColumn(line + 1));
	SetCursorPosition(cursor);
	SetSelection(cursor, cursor);
	mInteractiveStart = mInteractiveEnd = cursor;
	undo.mAfter = mState;
	AddUndo(undo);
	Colorize(line, 2);
}

void TextEditor::SetText(const std::string & aText)
{
	CloseAutocomplete();
	mLines.clear();
	mLines.emplace_back(Line());
	for (size_t i = 0; i < aText.size();)
	{
		const auto chr = static_cast<Char>(aText[i]);
		if (chr == '\r')
		{
			++i;
		}
		else if (chr == '\n')
		{
			mLines.emplace_back(Line());
			++i;
		}
		else
		{
			const auto sequenceLength = UTF8CharLength(chr);
			size_t sequenceEnd = i + 1;
			while (sequenceEnd < aText.size() &&
				sequenceEnd < i + (size_t)sequenceLength &&
				(static_cast<Char>(aText[sequenceEnd]) & 0xC0) == 0x80)
				++sequenceEnd;

			while (i < sequenceEnd)
				mLines.back().emplace_back(Glyph(static_cast<Char>(aText[i++]), PaletteIndex::Default));
		}
	}

	mTextChanged = true;
	mScrollToTop = true;

	mUndoBuffer.clear();
	mUndoIndex = 0;

	Colorize();
}

void TextEditor::SetTextLines(const std::vector<std::string> & aLines)
{
	CloseAutocomplete();
	mLines.clear();

	if (aLines.empty())
	{
		mLines.emplace_back(Line());
	}
	else
	{
		mLines.resize(aLines.size());

		for (size_t i = 0; i < aLines.size(); ++i)
		{
			const std::string & aLine = aLines[i];

			mLines[i].reserve(aLine.size());
			for (size_t j = 0; j < aLine.size(); ++j)
				mLines[i].emplace_back(Glyph(aLine[j], PaletteIndex::Default));
		}
	}

	mTextChanged = true;
	mScrollToTop = true;

	mUndoBuffer.clear();
	mUndoIndex = 0;

	Colorize();
}

void TextEditor::EnterCharacter(ImWchar aChar, bool aShift)
{
	assert(!mReadOnly);

	UndoRecord u;

	u.mBefore = mState;

	if (aChar == '\t' && aShift && !HasSelection())
	{
		auto lineIndex = mState.mCursorPosition.mLine;
		auto& line = mLines[lineIndex];
		size_t removeCount = 0;
		if (!line.empty() && line.front().mChar == '\t')
			removeCount = 1;
		else
		{
			while (removeCount < line.size() && removeCount < (size_t)mTabSize &&
				line[removeCount].mChar == ' ')
				++removeCount;
		}

		if (removeCount == 0)
			return;

		u.mRemovedStart = Coordinates(lineIndex, 0);
		u.mRemovedEnd = Coordinates(lineIndex, GetCharacterColumn(lineIndex, (int)removeCount));
		u.mRemoved = GetText(u.mRemovedStart, u.mRemovedEnd);
		line.erase(line.begin(), line.begin() + removeCount);

		auto cursor = mState.mCursorPosition;
		cursor.mColumn = std::max(0, cursor.mColumn - u.mRemovedEnd.mColumn);
		SetSelection(cursor, cursor);
		SetCursorPosition(cursor);
		u.mAfter = mState;
		AddUndo(u);

		Colorize(lineIndex, 1);
		EnsureCursorVisible();
		return;
	}

	if (HasSelection())
	{
		if (aChar == '\t' && (mState.mSelectionStart.mLine != mState.mSelectionEnd.mLine || aShift))
		{

			auto start = mState.mSelectionStart;
			auto end = mState.mSelectionEnd;
			auto originalEnd = end;

			if (start > end)
				std::swap(start, end);
			start.mColumn = 0;
			//			end.mColumn = end.mLine < mLines.size() ? mLines[end.mLine].size() : 0;
			if (end.mColumn == 0 && end.mLine > 0)
				--end.mLine;
			if (end.mLine >= (int)mLines.size())
				end.mLine = mLines.empty() ? 0 : (int)mLines.size() - 1;
			end.mColumn = GetLineMaxColumn(end.mLine);

			//if (end.mColumn >= GetLineMaxColumn(end.mLine))
			//	end.mColumn = GetLineMaxColumn(end.mLine) - 1;

			u.mRemovedStart = start;
			u.mRemovedEnd = end;
			u.mRemoved = GetText(start, end);

			bool modified = false;

			for (int i = start.mLine; i <= end.mLine; i++)
			{
				auto& line = mLines[i];
				if (aShift)
				{
					if (!line.empty())
					{
						if (line.front().mChar == '\t')
						{
							line.erase(line.begin());
							modified = true;
						}
						else
						{
							for (int j = 0; j < mTabSize && !line.empty() && line.front().mChar == ' '; j++)
							{
								line.erase(line.begin());
								modified = true;
							}
						}
					}
				}
				else
				{
					line.insert(line.begin(), Glyph('\t', TextEditor::PaletteIndex::Background));
					modified = true;
				}
			}

			if (modified)
			{
				start = Coordinates(start.mLine, GetCharacterColumn(start.mLine, 0));
				Coordinates rangeEnd;
				if (originalEnd.mColumn != 0)
				{
					end = Coordinates(end.mLine, GetLineMaxColumn(end.mLine));
					rangeEnd = end;
					u.mAdded = GetText(start, end);
				}
				else
				{
					end = Coordinates(originalEnd.mLine, 0);
					rangeEnd = Coordinates(end.mLine - 1, GetLineMaxColumn(end.mLine - 1));
					u.mAdded = GetText(start, rangeEnd);
				}

				u.mAddedStart = start;
				u.mAddedEnd = rangeEnd;
				u.mAfter = mState;

				mState.mSelectionStart = start;
				mState.mSelectionEnd = end;
				AddUndo(u);

				mTextChanged = true;

				EnsureCursorVisible();
			}

			return;
		} // c == '\t'
		else
		{
			u.mRemoved = GetSelectedText();
			u.mRemovedStart = mState.mSelectionStart;
			u.mRemovedEnd = mState.mSelectionEnd;
			DeleteSelection();
		}
	} // HasSelection

	auto coord = GetActualCursorCoordinates();
	u.mAddedStart = coord;

	assert(!mLines.empty());

	if (aChar == '\n')
	{
		auto& line = mLines[coord.mLine];
		const int cindex = GetCharacterIndex(coord);
		size_t whitespaceEnd = 0;
		if (mLanguageDefinition.mAutoIndentation)
		{
			while (whitespaceEnd < line.size() &&
				(line[whitespaceEnd].mChar == ' ' || line[whitespaceEnd].mChar == '\t'))
				++whitespaceEnd;
		}

		Line indentation(line.begin(), line.begin() + whitespaceEnd);
		Line extraIndent;
		if (mLanguageDefinition.mAutoIndentation)
		{
			const bool usesTabs = std::any_of(indentation.begin(), indentation.end(),
				[](const Glyph& glyph) { return glyph.mChar == '\t'; });
			if (usesTabs)
				extraIndent.push_back(Glyph('\t', PaletteIndex::Default));
			else
				for (int i = 0; i < std::max(mTabSize, 1); ++i)
					extraIndent.push_back(Glyph(' ', PaletteIndex::Default));
		}

		int previousNonWhitespace = cindex - 1;
		while (previousNonWhitespace >= 0 &&
			(line[previousNonWhitespace].mChar == ' ' || line[previousNonWhitespace].mChar == '\t'))
			--previousNonWhitespace;
		int nextNonWhitespace = cindex;
		while (nextNonWhitespace < (int)line.size() &&
			(line[nextNonWhitespace].mChar == ' ' || line[nextNonWhitespace].mChar == '\t'))
			++nextNonWhitespace;
		const bool afterOpenBrace = previousNonWhitespace >= 0 &&
			line[previousNonWhitespace].mChar == '{';
		const bool beforeCloseBrace = nextNonWhitespace < (int)line.size() &&
			line[nextNonWhitespace].mChar == '}';
		const bool emptyBlock = afterOpenBrace && beforeCloseBrace && u.mRemoved.empty();

		if (emptyBlock)
		{
			const Coordinates originalEnd(coord.mLine, GetLineMaxColumn(coord.mLine));
			u.mRemovedStart = coord;
			u.mRemovedEnd = originalEnd;
			u.mRemoved = GetText(u.mRemovedStart, u.mRemovedEnd);

			Line closingLine(indentation);
			closingLine.insert(closingLine.end(), line.begin() + nextNonWhitespace, line.end());
			line.erase(line.begin() + cindex, line.end());

			InsertLine(coord.mLine + 1);
			InsertLine(coord.mLine + 2);
			auto& innerLine = mLines[coord.mLine + 1];
			innerLine = indentation;
			innerLine.insert(innerLine.end(), extraIndent.begin(), extraIndent.end());
			mLines[coord.mLine + 2] = std::move(closingLine);

			const Coordinates cursor(coord.mLine + 1, GetLineMaxColumn(coord.mLine + 1));
			SetCursorPosition(cursor);
			SetSelection(cursor, cursor);
			mInteractiveStart = mInteractiveEnd = cursor;
			u.mAddedEnd = Coordinates(coord.mLine + 2, GetLineMaxColumn(coord.mLine + 2));
		}
		else
		{
			InsertLine(coord.mLine + 1);
			auto& originalLine = mLines[coord.mLine];
			auto& newLine = mLines[coord.mLine + 1];
			newLine = indentation;
			if (afterOpenBrace)
				newLine.insert(newLine.end(), extraIndent.begin(), extraIndent.end());
			const size_t cursorIndex = newLine.size();
			newLine.insert(newLine.end(), originalLine.begin() + cindex, originalLine.end());
			originalLine.erase(originalLine.begin() + cindex, originalLine.end());

			const Coordinates cursor(coord.mLine + 1, GetCharacterColumn(coord.mLine + 1, (int)cursorIndex));
			SetCursorPosition(cursor);
			SetSelection(cursor, cursor);
			mInteractiveStart = mInteractiveEnd = cursor;
			u.mAddedEnd = cursor;
		}

		u.mAdded = GetText(u.mAddedStart, u.mAddedEnd);
	}
	else
	{
		char buf[7];
		int e = ImTextCharToUtf8(buf, 7, aChar);
		if (e > 0)
		{
			buf[e] = '\0';
			auto& line = mLines[coord.mLine];
			auto cindex = GetCharacterIndex(coord);

			if (mOverwrite && cindex < (int)line.size())
			{
				auto d = UTF8CharLength(line[cindex].mChar);

				u.mRemovedStart = mState.mCursorPosition;
				u.mRemovedEnd = Coordinates(coord.mLine, GetCharacterColumn(coord.mLine, cindex + d));

				while (d-- > 0 && cindex < (int)line.size())
				{
					u.mRemoved += line[cindex].mChar;
					line.erase(line.begin() + cindex);
				}
			}

			for (auto p = buf; *p != '\0'; p++, ++cindex)
				line.insert(line.begin() + cindex, Glyph(*p, PaletteIndex::Default));
			u.mAdded = buf;

			SetCursorPosition(Coordinates(coord.mLine, GetCharacterColumn(coord.mLine, cindex)));
		}
		else
			return;
	}

	mTextChanged = true;

	if (aChar != '\n')
		u.mAddedEnd = GetActualCursorCoordinates();
	u.mAfter = mState;

	AddUndo(u, aChar != '\n' && aChar != '\t' && u.mRemoved.empty());

	Colorize(coord.mLine - 1, 3);
	EnsureCursorVisible();
}

void TextEditor::SetReadOnly(bool aValue)
{
	mReadOnly = aValue;
}

void TextEditor::SetColorizerEnable(bool aValue)
{
	mColorizerEnabled = aValue;
}

void TextEditor::SetCursorPosition(const Coordinates & aPosition)
{
	if (mState.mCursorPosition != aPosition)
	{
		mState.mCursorPosition = aPosition;
		mCursorPositionChanged = true;
		EnsureCursorVisible();
	}
}

void TextEditor::SetSelectionStart(const Coordinates & aPosition)
{
	mState.mSelectionStart = SanitizeCoordinates(aPosition);
	if (mState.mSelectionStart > mState.mSelectionEnd)
		std::swap(mState.mSelectionStart, mState.mSelectionEnd);
}

void TextEditor::SetSelectionEnd(const Coordinates & aPosition)
{
	mState.mSelectionEnd = SanitizeCoordinates(aPosition);
	if (mState.mSelectionStart > mState.mSelectionEnd)
		std::swap(mState.mSelectionStart, mState.mSelectionEnd);
}

void TextEditor::SetSelection(const Coordinates & aStart, const Coordinates & aEnd, SelectionMode aMode)
{
	auto oldSelStart = mState.mSelectionStart;
	auto oldSelEnd = mState.mSelectionEnd;

	mState.mSelectionStart = SanitizeCoordinates(aStart);
	mState.mSelectionEnd = SanitizeCoordinates(aEnd);
	if (mState.mSelectionStart > mState.mSelectionEnd)
		std::swap(mState.mSelectionStart, mState.mSelectionEnd);

	switch (aMode)
	{
	case TextEditor::SelectionMode::Normal:
		break;
	case TextEditor::SelectionMode::Word:
	{
		mState.mSelectionStart = FindWordStart(mState.mSelectionStart);
		if (!IsOnWordBoundary(mState.mSelectionEnd))
			mState.mSelectionEnd = FindWordEnd(FindWordStart(mState.mSelectionEnd));
		break;
	}
	case TextEditor::SelectionMode::Line:
	{
		const auto lineNo = mState.mSelectionEnd.mLine;
		const auto lineSize = (size_t)lineNo < mLines.size() ? mLines[lineNo].size() : 0;
		mState.mSelectionStart = Coordinates(mState.mSelectionStart.mLine, 0);
		mState.mSelectionEnd = Coordinates(lineNo, GetLineMaxColumn(lineNo));
		break;
	}
	default:
		break;
	}

	if (mState.mSelectionStart != oldSelStart ||
		mState.mSelectionEnd != oldSelEnd)
		mCursorPositionChanged = true;
}

void TextEditor::SetTabSize(int aValue)
{
	mTabSize = std::max(0, std::min(32, aValue));
}

void TextEditor::InsertText(const std::string & aValue)
{
	InsertText(aValue.c_str());
}

void TextEditor::InsertText(const char * aValue)
{
	if (aValue == nullptr)
		return;

	auto pos = GetActualCursorCoordinates();
	auto start = std::min(pos, mState.mSelectionStart);
	int totalLines = pos.mLine - start.mLine;

	totalLines += InsertTextAt(pos, aValue);

	SetSelection(pos, pos);
	SetCursorPosition(pos);
	Colorize(start.mLine - 1, totalLines + 2);
}

void TextEditor::DeleteSelection()
{
	assert(mState.mSelectionEnd >= mState.mSelectionStart);

	if (mState.mSelectionEnd == mState.mSelectionStart)
		return;

	DeleteRange(mState.mSelectionStart, mState.mSelectionEnd);

	SetSelection(mState.mSelectionStart, mState.mSelectionStart);
	SetCursorPosition(mState.mSelectionStart);
	Colorize(mState.mSelectionStart.mLine, 1);
}

void TextEditor::MoveUp(int aAmount, bool aSelect)
{
	auto oldPos = mState.mCursorPosition;
	mState.mCursorPosition.mLine = std::max(0, mState.mCursorPosition.mLine - aAmount);
	if (oldPos != mState.mCursorPosition)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveStart)
				mInteractiveStart = mState.mCursorPosition;
			else if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mState.mCursorPosition;
			else
			{
				mInteractiveStart = mState.mCursorPosition;
				mInteractiveEnd = oldPos;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);

		EnsureCursorVisible();
	}
}

void TextEditor::MoveDown(int aAmount, bool aSelect)
{
	assert(mState.mCursorPosition.mColumn >= 0);
	auto oldPos = mState.mCursorPosition;
	mState.mCursorPosition.mLine = std::max(0, std::min((int)mLines.size() - 1, mState.mCursorPosition.mLine + aAmount));

	if (mState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mState.mCursorPosition;
			else if (oldPos == mInteractiveStart)
				mInteractiveStart = mState.mCursorPosition;
			else
			{
				mInteractiveStart = oldPos;
				mInteractiveEnd = mState.mCursorPosition;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);

		EnsureCursorVisible();
	}
}

static bool IsUTFSequence(char c)
{
	return (c & 0xC0) == 0x80;
}

void TextEditor::MoveLeft(int aAmount, bool aSelect, bool aWordMode)
{
	if (mLines.empty())
		return;

	auto oldPos = mState.mCursorPosition;
	mState.mCursorPosition = GetActualCursorCoordinates();
	auto line = mState.mCursorPosition.mLine;
	auto cindex = GetCharacterIndex(mState.mCursorPosition);

	while (aAmount-- > 0)
	{
		if (cindex == 0)
		{
			if (line > 0)
			{
				--line;
				if ((int)mLines.size() > line)
					cindex = (int)mLines[line].size();
				else
					cindex = 0;
			}
		}
		else
		{
			--cindex;
			if (cindex > 0)
			{
				if ((int)mLines.size() > line)
				{
					while (cindex > 0 && IsUTFSequence(mLines[line][cindex].mChar))
						--cindex;
				}
			}
		}

		mState.mCursorPosition = Coordinates(line, GetCharacterColumn(line, cindex));
		if (aWordMode)
		{
			mState.mCursorPosition = FindWordStart(mState.mCursorPosition);
			cindex = GetCharacterIndex(mState.mCursorPosition);
		}
	}

	mState.mCursorPosition = Coordinates(line, GetCharacterColumn(line, cindex));

	assert(mState.mCursorPosition.mColumn >= 0);
	if (aSelect)
	{
		if (oldPos == mInteractiveStart)
			mInteractiveStart = mState.mCursorPosition;
		else if (oldPos == mInteractiveEnd)
			mInteractiveEnd = mState.mCursorPosition;
		else
		{
			mInteractiveStart = mState.mCursorPosition;
			mInteractiveEnd = oldPos;
		}
	}
	else
		mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
	SetSelection(mInteractiveStart, mInteractiveEnd, aSelect && aWordMode ? SelectionMode::Word : SelectionMode::Normal);

	EnsureCursorVisible();
}

void TextEditor::MoveRight(int aAmount, bool aSelect, bool aWordMode)
{
	auto oldPos = mState.mCursorPosition;

	if (mLines.empty() || oldPos.mLine >= mLines.size())
		return;

	auto cindex = GetCharacterIndex(mState.mCursorPosition);
	while (aAmount-- > 0)
	{
		auto lindex = mState.mCursorPosition.mLine;
		auto& line = mLines[lindex];

		if (cindex >= line.size())
		{
			if (mState.mCursorPosition.mLine < mLines.size() - 1)
			{
				mState.mCursorPosition.mLine = std::max(0, std::min((int)mLines.size() - 1, mState.mCursorPosition.mLine + 1));
				mState.mCursorPosition.mColumn = 0;
			}
			else
				return;
		}
		else
		{
			cindex += UTF8CharLength(line[cindex].mChar);
			mState.mCursorPosition = Coordinates(lindex, GetCharacterColumn(lindex, cindex));
			if (aWordMode)
				mState.mCursorPosition = FindNextWord(mState.mCursorPosition);
		}
	}

	if (aSelect)
	{
		if (oldPos == mInteractiveEnd)
			mInteractiveEnd = SanitizeCoordinates(mState.mCursorPosition);
		else if (oldPos == mInteractiveStart)
			mInteractiveStart = mState.mCursorPosition;
		else
		{
			mInteractiveStart = oldPos;
			mInteractiveEnd = mState.mCursorPosition;
		}
	}
	else
		mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
	SetSelection(mInteractiveStart, mInteractiveEnd, aSelect && aWordMode ? SelectionMode::Word : SelectionMode::Normal);

	EnsureCursorVisible();
}

void TextEditor::MoveTop(bool aSelect)
{
	auto oldPos = mState.mCursorPosition;
	SetCursorPosition(Coordinates(0, 0));

	if (mState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			mInteractiveEnd = oldPos;
			mInteractiveStart = mState.mCursorPosition;
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);
	}
}

void TextEditor::MoveBottom(bool aSelect)
{
	auto oldPos = GetCursorPosition();
	auto lastLine = (int)mLines.size() - 1;
	auto newPos = Coordinates(lastLine, GetLineMaxColumn(lastLine));
	SetCursorPosition(newPos);
	if (aSelect)
	{
		mInteractiveStart = oldPos;
		mInteractiveEnd = newPos;
	}
	else
		mInteractiveStart = mInteractiveEnd = newPos;
	SetSelection(mInteractiveStart, mInteractiveEnd);
}

void TextEditor::MoveHome(bool aSelect)
{
	auto oldPos = mState.mCursorPosition;
	SetCursorPosition(Coordinates(mState.mCursorPosition.mLine, 0));

	if (mState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveStart)
				mInteractiveStart = mState.mCursorPosition;
			else if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mState.mCursorPosition;
			else
			{
				mInteractiveStart = mState.mCursorPosition;
				mInteractiveEnd = oldPos;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);
	}
}

void TextEditor::MoveEnd(bool aSelect)
{
	auto oldPos = mState.mCursorPosition;
	SetCursorPosition(Coordinates(mState.mCursorPosition.mLine, GetLineMaxColumn(oldPos.mLine)));

	if (mState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mState.mCursorPosition;
			else if (oldPos == mInteractiveStart)
				mInteractiveStart = mState.mCursorPosition;
			else
			{
				mInteractiveStart = oldPos;
				mInteractiveEnd = mState.mCursorPosition;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);
	}
}

void TextEditor::Delete()
{
	assert(!mReadOnly);

	if (mLines.empty())
		return;

	UndoRecord u;
	u.mBefore = mState;

	if (HasSelection())
	{
		u.mRemoved = GetSelectedText();
		u.mRemovedStart = mState.mSelectionStart;
		u.mRemovedEnd = mState.mSelectionEnd;

		DeleteSelection();
	}
	else
	{
		auto pos = GetActualCursorCoordinates();
		SetCursorPosition(pos);
		auto& line = mLines[pos.mLine];

		if (pos.mColumn == GetLineMaxColumn(pos.mLine))
		{
			if (pos.mLine == (int)mLines.size() - 1)
				return;

			u.mRemoved = '\n';
			u.mRemovedStart = u.mRemovedEnd = GetActualCursorCoordinates();
			Advance(u.mRemovedEnd);

			auto& nextLine = mLines[pos.mLine + 1];
			line.insert(line.end(), nextLine.begin(), nextLine.end());
			RemoveLine(pos.mLine + 1);
		}
		else
		{
			auto cindex = GetCharacterIndex(pos);
			u.mRemovedStart = u.mRemovedEnd = GetActualCursorCoordinates();
			u.mRemovedEnd.mColumn++;
			u.mRemoved = GetText(u.mRemovedStart, u.mRemovedEnd);

			auto d = UTF8CharLength(line[cindex].mChar);
			while (d-- > 0 && cindex < (int)line.size())
				line.erase(line.begin() + cindex);
		}

		mTextChanged = true;

		Colorize(pos.mLine, 1);
	}

	u.mAfter = mState;
	AddUndo(u);
}

void TextEditor::Backspace()
{
	assert(!mReadOnly);

	if (mLines.empty())
		return;

	if (!HasSelection())
	{
		const auto position = GetActualCursorCoordinates();
		const int index = GetCharacterIndex(position);
		const auto& line = mLines[position.mLine];
		if (position.mColumn > 0 && index > 0 && index < (int)line.size())
		{
			const char previous = (char)line[index - 1].mChar;
			const char next = (char)line[index].mChar;
			const bool emptyPair =
				(previous == '(' && next == ')') || (previous == '[' && next == ']') ||
				(previous == '{' && next == '}') || (previous == '"' && next == '"') ||
				(previous == '\'' && next == '\'');
			if (emptyPair)
			{
				UndoRecord pairUndo;
				pairUndo.mBefore = mState;
				pairUndo.mRemovedStart = Coordinates(position.mLine, position.mColumn - 1);
				pairUndo.mRemovedEnd = Coordinates(position.mLine, position.mColumn + 1);
				pairUndo.mRemoved = GetText(pairUndo.mRemovedStart, pairUndo.mRemovedEnd);
				DeleteRange(pairUndo.mRemovedStart, pairUndo.mRemovedEnd);
				SetCursorPosition(pairUndo.mRemovedStart);
				SetSelection(pairUndo.mRemovedStart, pairUndo.mRemovedStart);
				mInteractiveStart = mInteractiveEnd = pairUndo.mRemovedStart;
				pairUndo.mAfter = mState;
				AddUndo(pairUndo);
				Colorize(position.mLine, 1);
				EnsureCursorVisible();
				return;
			}
		}
	}

	UndoRecord u;
	u.mBefore = mState;

	if (HasSelection())
	{
		u.mRemoved = GetSelectedText();
		u.mRemovedStart = mState.mSelectionStart;
		u.mRemovedEnd = mState.mSelectionEnd;

		DeleteSelection();
	}
	else
	{
		auto pos = GetActualCursorCoordinates();
		SetCursorPosition(pos);

		if (mState.mCursorPosition.mColumn == 0)
		{
			if (mState.mCursorPosition.mLine == 0)
				return;

			u.mRemoved = '\n';
			u.mRemovedStart = u.mRemovedEnd = Coordinates(pos.mLine - 1, GetLineMaxColumn(pos.mLine - 1));
			Advance(u.mRemovedEnd);

			auto& line = mLines[mState.mCursorPosition.mLine];
			auto& prevLine = mLines[mState.mCursorPosition.mLine - 1];
			auto prevSize = GetLineMaxColumn(mState.mCursorPosition.mLine - 1);
			prevLine.insert(prevLine.end(), line.begin(), line.end());

			ErrorMarkers etmp;
			for (auto& i : mErrorMarkers)
				etmp.insert(ErrorMarkers::value_type(i.first - 1 == mState.mCursorPosition.mLine ? i.first - 1 : i.first, i.second));
			mErrorMarkers = std::move(etmp);

			RemoveLine(mState.mCursorPosition.mLine);
			--mState.mCursorPosition.mLine;
			mState.mCursorPosition.mColumn = prevSize;
		}
		else
		{
			auto& line = mLines[mState.mCursorPosition.mLine];
			auto cindex = GetCharacterIndex(pos) - 1;
			auto cend = cindex + 1;
			while (cindex > 0 && IsUTFSequence(line[cindex].mChar))
				--cindex;

			//if (cindex > 0 && UTF8CharLength(line[cindex].mChar) > 1)
			//	--cindex;

			u.mRemovedStart = u.mRemovedEnd = GetActualCursorCoordinates();
			--u.mRemovedStart.mColumn;
			--mState.mCursorPosition.mColumn;

			while (cindex < line.size() && cend-- > cindex)
			{
				u.mRemoved += line[cindex].mChar;
				line.erase(line.begin() + cindex);
			}
		}

		mTextChanged = true;

		EnsureCursorVisible();
		Colorize(mState.mCursorPosition.mLine, 1);
	}

	u.mAfter = mState;
	AddUndo(u);
}

void TextEditor::SelectWordUnderCursor()
{
	auto c = GetCursorPosition();
	SetSelection(FindWordStart(c), FindWordEnd(c));
}

void TextEditor::SelectAll()
{
	SetSelection(Coordinates(0, 0), Coordinates((int)mLines.size(), 0));
}

bool TextEditor::HasSelection() const
{
	return mState.mSelectionEnd > mState.mSelectionStart;
}

void TextEditor::Copy()
{
	if (HasSelection())
	{
		ImGui::SetClipboardText(GetSelectedText().c_str());
	}
	else
	{
		if (!mLines.empty())
		{
			std::string str;
			auto& line = mLines[GetActualCursorCoordinates().mLine];
			for (auto& g : line)
				str.push_back(g.mChar);
			ImGui::SetClipboardText(str.c_str());
		}
	}
}

void TextEditor::Cut()
{
	if (IsReadOnly())
	{
		Copy();
	}
	else
	{
		if (HasSelection())
		{
			UndoRecord u;
			u.mBefore = mState;
			u.mRemoved = GetSelectedText();
			u.mRemovedStart = mState.mSelectionStart;
			u.mRemovedEnd = mState.mSelectionEnd;

			Copy();
			DeleteSelection();

			u.mAfter = mState;
			AddUndo(u);
		}
	}
}

void TextEditor::Paste()
{
	if (IsReadOnly())
		return;

	auto clipText = ImGui::GetClipboardText();
	if (clipText != nullptr && strlen(clipText) > 0)
	{
		UndoRecord u;
		u.mBefore = mState;

		if (HasSelection())
		{
			u.mRemoved = GetSelectedText();
			u.mRemovedStart = mState.mSelectionStart;
			u.mRemovedEnd = mState.mSelectionEnd;
			DeleteSelection();
		}

		u.mAdded = clipText;
		u.mAddedStart = GetActualCursorCoordinates();

		InsertText(clipText);

		u.mAddedEnd = GetActualCursorCoordinates();
		u.mAfter = mState;
		AddUndo(u);
	}
}

bool TextEditor::CanUndo() const
{
	return !mReadOnly && mUndoIndex > 0;
}

bool TextEditor::CanRedo() const
{
	return !mReadOnly && mUndoIndex < (int)mUndoBuffer.size();
}

void TextEditor::Undo(int aSteps)
{
	while (CanUndo() && aSteps-- > 0)
		mUndoBuffer[--mUndoIndex].Undo(this);
}

void TextEditor::Redo(int aSteps)
{
	while (CanRedo() && aSteps-- > 0)
		mUndoBuffer[mUndoIndex++].Redo(this);
}

const TextEditor::Palette & TextEditor::GetDarkPalette()
{
	const static Palette p = { {
			0xff7f7f7f,	// Default
			0xffd69c56,	// Keyword	
			0xff00ff00,	// Number
			0xff7070e0,	// String
			0xff70a0e0, // Char literal
			0xffffffff, // Punctuation
			0xff408080,	// Preprocessor
			0xffaaaaaa, // Identifier
			0xff9bc64d, // Known identifier
			0xffc040a0, // Preproc identifier
			0xff206020, // Comment (single line)
			0xff406020, // Comment (multi line)
			0xff101010, // Background
			0xffe0e0e0, // Cursor
			0x80a06020, // Selection
			0x800020ff, // ErrorMarker
			0x40f08000, // Breakpoint
			0xff707000, // Line number
			0x40000000, // Current line fill
			0x40808080, // Current line fill (inactive)
			0x40a0a0a0, // Current line edge
		} };
	return p;
}

const TextEditor::Palette & TextEditor::GetLightPalette()
{
	const static Palette p = { {
			0xff7f7f7f,	// None
			0xffff0c06,	// Keyword	
			0xff008000,	// Number
			0xff2020a0,	// String
			0xff304070, // Char literal
			0xff000000, // Punctuation
			0xff406060,	// Preprocessor
			0xff404040, // Identifier
			0xff606010, // Known identifier
			0xffc040a0, // Preproc identifier
			0xff205020, // Comment (single line)
			0xff405020, // Comment (multi line)
			0xffffffff, // Background
			0xff000000, // Cursor
			0x80600000, // Selection
			0xa00010ff, // ErrorMarker
			0x80f08000, // Breakpoint
			0xff505000, // Line number
			0x40000000, // Current line fill
			0x40808080, // Current line fill (inactive)
			0x40000000, // Current line edge
		} };
	return p;
}

const TextEditor::Palette & TextEditor::GetRetroBluePalette()
{
	const static Palette p = { {
			0xff00ffff,	// None
			0xffffff00,	// Keyword	
			0xff00ff00,	// Number
			0xff808000,	// String
			0xff808000, // Char literal
			0xffffffff, // Punctuation
			0xff008000,	// Preprocessor
			0xff00ffff, // Identifier
			0xffffffff, // Known identifier
			0xffff00ff, // Preproc identifier
			0xff808080, // Comment (single line)
			0xff404040, // Comment (multi line)
			0xff800000, // Background
			0xff0080ff, // Cursor
			0x80ffff00, // Selection
			0xa00000ff, // ErrorMarker
			0x80ff8000, // Breakpoint
			0xff808000, // Line number
			0x40000000, // Current line fill
			0x40808080, // Current line fill (inactive)
			0x40000000, // Current line edge
		} };
	return p;
}


std::string TextEditor::GetText() const
{
	return GetText(Coordinates(), Coordinates((int)mLines.size(), 0));
}

std::vector<std::string> TextEditor::GetTextLines() const
{
	std::vector<std::string> result;

	result.reserve(mLines.size());

	for (auto & line : mLines)
	{
		std::string text;

		text.resize(line.size());

		for (size_t i = 0; i < line.size(); ++i)
			text[i] = line[i].mChar;

		result.emplace_back(std::move(text));
	}

	return result;
}

std::string TextEditor::GetSelectedText() const
{
	return GetText(mState.mSelectionStart, mState.mSelectionEnd);
}

std::string TextEditor::GetCurrentLineText()const
{
	auto lineLength = GetLineMaxColumn(mState.mCursorPosition.mLine);
	return GetText(
		Coordinates(mState.mCursorPosition.mLine, 0),
		Coordinates(mState.mCursorPosition.mLine, lineLength));
}

void TextEditor::ProcessInputs()
{
}

void TextEditor::Colorize(int aFromLine, int aLines)
{
	int toLine = aLines == -1 ? (int)mLines.size() : std::min((int)mLines.size(), aFromLine + aLines);
	mColorRangeMin = std::min(mColorRangeMin, aFromLine);
	mColorRangeMax = std::max(mColorRangeMax, toLine);
	mColorRangeMin = std::max(0, mColorRangeMin);
	mColorRangeMax = std::max(mColorRangeMin, mColorRangeMax);
	mCheckComments = true;
}

void TextEditor::ColorizeRange(int aFromLine, int aToLine)
{
	if (mLines.empty() || aFromLine >= aToLine)
		return;

	std::string buffer;
	std::cmatch results;
	std::string id;

	int endLine = std::max(0, std::min((int)mLines.size(), aToLine));
	for (int i = aFromLine; i < endLine; ++i)
	{
		auto& line = mLines[i];

		if (line.empty())
			continue;

		buffer.resize(line.size());
		for (size_t j = 0; j < line.size(); ++j)
		{
			auto& col = line[j];
			buffer[j] = col.mChar;
			col.mColorIndex = PaletteIndex::Default;
		}

		const char * bufferBegin = &buffer.front();
		const char * bufferEnd = bufferBegin + buffer.size();

		auto last = bufferEnd;

		for (auto first = bufferBegin; first != last; )
		{
			const char * token_begin = nullptr;
			const char * token_end = nullptr;
			PaletteIndex token_color = PaletteIndex::Default;

			bool hasTokenizeResult = false;

			if (mLanguageDefinition.mTokenize != nullptr)
			{
				if (mLanguageDefinition.mTokenize(first, last, token_begin, token_end, token_color))
					hasTokenizeResult = true;
			}

			if (hasTokenizeResult == false)
			{
				// todo : remove
				//printf("using regex for %.*s\n", first + 10 < last ? 10 : int(last - first), first);

				for (auto& p : mRegexList)
				{
					if (std::regex_search(first, last, results, p.first, std::regex_constants::match_continuous))
					{
						hasTokenizeResult = true;

						auto& v = *results.begin();
						token_begin = v.first;
						token_end = v.second;
						token_color = p.second;
						break;
					}
				}
			}

			if (hasTokenizeResult == false)
			{
				first++;
			}
			else
			{
				const size_t token_length = token_end - token_begin;

				if (token_color == PaletteIndex::Identifier)
				{
					id.assign(token_begin, token_end);

					// todo : allmost all language definitions use lower case to specify keywords, so shouldn't this use ::tolower ?
					if (!mLanguageDefinition.mCaseSensitive)
						std::transform(id.begin(), id.end(), id.begin(), ::toupper);

					if (!line[first - bufferBegin].mPreprocessor)
					{
						if (mLanguageDefinition.mKeywords.count(id) != 0)
							token_color = PaletteIndex::Keyword;
						else if (mLanguageDefinition.mIdentifiers.count(id) != 0)
							token_color = PaletteIndex::KnownIdentifier;
						else if (mLanguageDefinition.mPreprocIdentifiers.count(id) != 0)
							token_color = PaletteIndex::PreprocIdentifier;
					}
					else
					{
						if (mLanguageDefinition.mPreprocIdentifiers.count(id) != 0)
							token_color = PaletteIndex::PreprocIdentifier;
					}
				}

				for (size_t j = 0; j < token_length; ++j)
					line[(token_begin - bufferBegin) + j].mColorIndex = token_color;

				first = token_end;
			}
		}
	}
}

void TextEditor::ColorizeInternal()
{
	if (mLines.empty() || !mColorizerEnabled)
		return;

	if (mCheckComments)
	{
		auto endLine = mLines.size();
		auto endIndex = 0;
		auto commentStartLine = endLine;
		auto commentStartIndex = endIndex;
		auto withinString = false;
		auto withinSingleLineComment = false;
		auto withinPreproc = false;
		auto firstChar = true;			// there is no other non-whitespace characters in the line before
		auto concatenate = false;		// '\' on the very end of the line
		auto currentLine = 0;
		auto currentIndex = 0;
		while (currentLine < endLine || currentIndex < endIndex)
		{
			auto& line = mLines[currentLine];

			if (currentIndex == 0 && !concatenate)
			{
				withinSingleLineComment = false;
				withinPreproc = false;
				firstChar = true;
			}

			concatenate = false;

			if (!line.empty())
			{
				auto& g = line[currentIndex];
				auto c = g.mChar;

				if (c != mLanguageDefinition.mPreprocChar && !isspace(c))
					firstChar = false;

				if (currentIndex == (int)line.size() - 1 && line[line.size() - 1].mChar == '\\')
					concatenate = true;

				bool inComment = (commentStartLine < currentLine || (commentStartLine == currentLine && commentStartIndex <= currentIndex));

				if (withinString)
				{
					line[currentIndex].mMultiLineComment = inComment;

					if (c == '\"')
					{
						if (currentIndex + 1 < (int)line.size() && line[currentIndex + 1].mChar == '\"')
						{
							currentIndex += 1;
							if (currentIndex < (int)line.size())
								line[currentIndex].mMultiLineComment = inComment;
						}
						else
							withinString = false;
					}
					else if (c == '\\')
					{
						currentIndex += 1;
						if (currentIndex < (int)line.size())
							line[currentIndex].mMultiLineComment = inComment;
					}
				}
				else
				{
					if (firstChar && c == mLanguageDefinition.mPreprocChar)
						withinPreproc = true;

					if (c == '\"')
					{
						withinString = true;
						line[currentIndex].mMultiLineComment = inComment;
					}
					else
					{
						auto pred = [](const char& a, const Glyph& b) { return a == b.mChar; };
						auto from = line.begin() + currentIndex;
						auto& startStr = mLanguageDefinition.mCommentStart;
						auto& singleStartStr = mLanguageDefinition.mSingleLineComment;

						if (singleStartStr.size() > 0 &&
							currentIndex + singleStartStr.size() <= line.size() &&
							equals(singleStartStr.begin(), singleStartStr.end(), from, from + singleStartStr.size(), pred))
						{
							withinSingleLineComment = true;
						}
						else if (!withinSingleLineComment && currentIndex + startStr.size() <= line.size() &&
							equals(startStr.begin(), startStr.end(), from, from + startStr.size(), pred))
						{
							commentStartLine = currentLine;
							commentStartIndex = currentIndex;
						}

						inComment = inComment = (commentStartLine < currentLine || (commentStartLine == currentLine && commentStartIndex <= currentIndex));

						line[currentIndex].mMultiLineComment = inComment;
						line[currentIndex].mComment = withinSingleLineComment;

						auto& endStr = mLanguageDefinition.mCommentEnd;
						if (currentIndex + 1 >= (int)endStr.size() &&
							equals(endStr.begin(), endStr.end(), from + 1 - endStr.size(), from + 1, pred))
						{
							commentStartIndex = endIndex;
							commentStartLine = endLine;
						}
					}
				}
				line[currentIndex].mPreprocessor = withinPreproc;
				currentIndex += UTF8CharLength(c);
				if (currentIndex >= (int)line.size())
				{
					currentIndex = 0;
					++currentLine;
				}
			}
			else
			{
				currentIndex = 0;
				++currentLine;
			}
		}
		mCheckComments = false;
	}

	if (mColorRangeMin < mColorRangeMax)
	{
		const int increment = (mLanguageDefinition.mTokenize == nullptr) ? 10 : 10000;
		const int to = std::min(mColorRangeMin + increment, mColorRangeMax);
		ColorizeRange(mColorRangeMin, to);
		mColorRangeMin = to;

		if (mColorRangeMax == mColorRangeMin)
		{
			mColorRangeMin = std::numeric_limits<int>::max();
			mColorRangeMax = 0;
		}
		return;
	}
}

float TextEditor::TextDistanceToLineStart(const Coordinates& aFrom) const
{
	auto& line = mLines[aFrom.mLine];
	float distance = 0.0f;
	float spaceSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, " ", nullptr, nullptr).x;
	int colIndex = GetCharacterIndex(aFrom);
	for (size_t it = 0u; it < line.size() && it < colIndex; )
	{
		if (line[it].mChar == '\t')
		{
			distance = (1.0f + std::floor((1.0f + distance) / (float(mTabSize) * spaceSize))) * (float(mTabSize) * spaceSize);
			++it;
		}
		else
		{
			auto d = UTF8CharLength(line[it].mChar);
			char tempCString[7];
			int i = 0;
			for (; i < 6 && d-- > 0 && it < (int)line.size(); i++, it++)
				tempCString[i] = line[it].mChar;

			tempCString[i] = '\0';
			distance += ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, tempCString, nullptr, nullptr).x;
		}
	}

	return distance;
}

void TextEditor::EnsureCursorVisible()
{
	if (!mWithinRender)
	{
		mScrollToCursor = true;
		return;
	}

	float scrollX = ImGui::GetScrollX();
	float scrollY = ImGui::GetScrollY();

	auto height = ImGui::GetWindowHeight();
	auto width = ImGui::GetWindowWidth();

	auto top = 1 + (int)ceil(scrollY / mCharAdvance.y);
	auto bottom = (int)ceil((scrollY + height) / mCharAdvance.y);

	auto left = (int)ceil(scrollX / mCharAdvance.x);
	auto right = (int)ceil((scrollX + width) / mCharAdvance.x);

	auto pos = GetActualCursorCoordinates();
	auto len = TextDistanceToLineStart(pos);

	if (pos.mLine < top)
		ImGui::SetScrollY(std::max(0.0f, (pos.mLine - 1) * mCharAdvance.y));
	if (pos.mLine > bottom - 4)
		ImGui::SetScrollY(std::max(0.0f, (pos.mLine + 4) * mCharAdvance.y - height));
	if (len + mTextStart < left + 4)
		ImGui::SetScrollX(std::max(0.0f, len + mTextStart - 4));
	if (len + mTextStart > right - 4)
		ImGui::SetScrollX(std::max(0.0f, len + mTextStart + 4 - width));
}

int TextEditor::GetPageSize() const
{
	auto height = ImGui::GetWindowHeight() - 20.0f;
	return (int)floor(height / mCharAdvance.y);
}

TextEditor::UndoRecord::UndoRecord(
	const std::string& aAdded,
	const TextEditor::Coordinates aAddedStart,
	const TextEditor::Coordinates aAddedEnd,
	const std::string& aRemoved,
	const TextEditor::Coordinates aRemovedStart,
	const TextEditor::Coordinates aRemovedEnd,
	TextEditor::EditorState& aBefore,
	TextEditor::EditorState& aAfter)
	: mAdded(aAdded)
	, mAddedStart(aAddedStart)
	, mAddedEnd(aAddedEnd)
	, mRemoved(aRemoved)
	, mRemovedStart(aRemovedStart)
	, mRemovedEnd(aRemovedEnd)
	, mBefore(aBefore)
	, mAfter(aAfter)
{
	assert(mAddedStart <= mAddedEnd);
	assert(mRemovedStart <= mRemovedEnd);
}

void TextEditor::UndoRecord::Undo(TextEditor * aEditor)
{
	if (!mAdded.empty())
	{
		aEditor->DeleteRange(mAddedStart, mAddedEnd);
		aEditor->Colorize(mAddedStart.mLine - 1, mAddedEnd.mLine - mAddedStart.mLine + 2);
	}

	if (!mRemoved.empty())
	{
		auto start = mRemovedStart;
		aEditor->InsertTextAt(start, mRemoved.c_str());
		aEditor->Colorize(mRemovedStart.mLine - 1, mRemovedEnd.mLine - mRemovedStart.mLine + 2);
	}

	aEditor->mState = mBefore;
	aEditor->EnsureCursorVisible();

}

void TextEditor::UndoRecord::Redo(TextEditor * aEditor)
{
	if (!mRemoved.empty())
	{
		aEditor->DeleteRange(mRemovedStart, mRemovedEnd);
		aEditor->Colorize(mRemovedStart.mLine - 1, mRemovedEnd.mLine - mRemovedStart.mLine + 1);
	}

	if (!mAdded.empty())
	{
		auto start = mAddedStart;
		aEditor->InsertTextAt(start, mAdded.c_str());
		aEditor->Colorize(mAddedStart.mLine - 1, mAddedEnd.mLine - mAddedStart.mLine + 1);
	}

	aEditor->mState = mAfter;
	aEditor->EnsureCursorVisible();
}

static bool TokenizeCStyleString(const char * in_begin, const char * in_end, const char *& out_begin, const char *& out_end)
{
	const char * p = in_begin;

	if (*p == '"')
	{
		p++;

		while (p < in_end)
		{
			// handle end of string
			if (*p == '"')
			{
				out_begin = in_begin;
				out_end = p + 1;
				return true;
			}

			// handle escape character for "
			if (*p == '\\' && p + 1 < in_end && p[1] == '"')
				p++;

			p++;
		}
	}

	return false;
}

static bool TokenizeCStyleCharacterLiteral(const char * in_begin, const char * in_end, const char *& out_begin, const char *& out_end)
{
	const char * p = in_begin;

	if (*p == '\'')
	{
		p++;

		// handle escape characters
		if (p < in_end && *p == '\\')
			p++;

		if (p < in_end)
			p++;

		// handle end of character literal
		if (p < in_end && *p == '\'')
		{
			out_begin = in_begin;
			out_end = p + 1;
			return true;
		}
	}

	return false;
}

static bool TokenizeCStyleIdentifier(const char * in_begin, const char * in_end, const char *& out_begin, const char *& out_end)
{
	const char * p = in_begin;

	if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '_')
	{
		p++;

		while ((p < in_end) && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_'))
			p++;

		out_begin = in_begin;
		out_end = p;
		return true;
	}

	return false;
}

static bool TokenizeCStyleNumber(const char * in_begin, const char * in_end, const char *& out_begin, const char *& out_end)
{
	const char * p = in_begin;

	const bool startsWithNumber = *p >= '0' && *p <= '9';

	if (*p != '+' && *p != '-' && !startsWithNumber)
		return false;

	p++;

	bool hasNumber = startsWithNumber;

	while (p < in_end && (*p >= '0' && *p <= '9'))
	{
		hasNumber = true;

		p++;
	}

	if (hasNumber == false)
		return false;

	bool isFloat = false;
	bool isHex = false;
	bool isBinary = false;

	if (p < in_end)
	{
		if (*p == '.')
		{
			isFloat = true;

			p++;

			while (p < in_end && (*p >= '0' && *p <= '9'))
				p++;
		}
		else if (*p == 'x' || *p == 'X')
		{
			// hex formatted integer of the type 0xef80

			isHex = true;

			p++;

			while (p < in_end && ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')))
				p++;
		}
		else if (*p == 'b' || *p == 'B')
		{
			// binary formatted integer of the type 0b01011101

			isBinary = true;

			p++;

			while (p < in_end && (*p >= '0' && *p <= '1'))
				p++;
		}
	}

	if (isHex == false && isBinary == false)
	{
		// floating point exponent
		if (p < in_end && (*p == 'e' || *p == 'E'))
		{
			isFloat = true;

			p++;

			if (p < in_end && (*p == '+' || *p == '-'))
				p++;

			bool hasDigits = false;

			while (p < in_end && (*p >= '0' && *p <= '9'))
			{
				hasDigits = true;

				p++;
			}

			if (hasDigits == false)
				return false;
		}

		// single precision floating point type
		if (p < in_end && *p == 'f')
			p++;
	}

	if (isFloat == false)
	{
		// integer size type
		while (p < in_end && (*p == 'u' || *p == 'U' || *p == 'l' || *p == 'L'))
			p++;
	}

	out_begin = in_begin;
	out_end = p;
	return true;
}

static bool TokenizeCStylePunctuation(const char * in_begin, const char * in_end, const char *& out_begin, const char *& out_end)
{
	(void)in_end;

	switch (*in_begin)
	{
	case '[':
	case ']':
	case '{':
	case '}':
	case '!':
	case '%':
	case '^':
	case '&':
	case '*':
	case '(':
	case ')':
	case '-':
	case '+':
	case '=':
	case '~':
	case '|':
	case '<':
	case '>':
	case '?':
	case ':':
	case '/':
	case ';':
	case ',':
	case '.':
		out_begin = in_begin;
		out_end = in_begin + 1;
		return true;
	}

	return false;
}

const TextEditor::LanguageDefinition& TextEditor::LanguageDefinition::CPlusPlus(const std::vector<const char*>& identifiers_)
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const std::vector<const char*> cppKeywords = {
			"alignas", "alignof", "and", "and_eq", "asm", "atomic_cancel", "atomic_commit", "atomic_noexcept", "auto", "bitand", "bitor", "bool", "break", "case", "catch", "char", "char16_t", "char32_t", "class",
			"compl", "concept", "const", "constexpr", "const_cast", "continue", "decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "float",
			"for", "friend", "goto", "if", "import", "inline", "int", "long", "module", "mutable", "namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private", "protected", "public",
			"register", "reinterpret_cast", "requires", "return", "short", "signed", "sizeof", "static", "static_assert", "static_cast", "struct", "switch", "synchronized", "template", "this", "thread_local",
			"throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq"
		};
		
		for (auto& k : cppKeywords)
			langDef.mKeywords.insert(k);

		static std::vector<const char*> identifiers = {
			"abort", "abs", "acos", "asin", "atan", "atexit", "atof", "atoi", "atol", "ceil", "clock", "cosh", "ctime", "div", "exit", "fabs", "floor", "fmod", "getchar", "getenv", "isalnum", "isalpha", "isdigit", "isgraph",
			"ispunct", "isspace", "isupper", "kbhit", "log10", "log2", "log", "memcmp", "modf", "pow", "printf", "sprintf", "snprintf", "putchar", "putenv", "puts", "rand", "remove", "rename", "sinh", "sqrt", "srand", "strcat", "strcmp", "strerror", "time", "tolower", "toupper",
			"std", "string", "vector", "map", "unordered_map", "set", "unordered_set", "min", "max"
		};
		
		for (const auto& i : identifiers_)
		{
			identifiers.push_back(i);
		}
		
		for (auto& k : identifiers)
		{
			Identifier id;
			id.mDeclaration = "Built-in function";
			langDef.mIdentifiers.insert(std::make_pair(std::string(k), id));
		}

		langDef.mTokenize = [](const char * in_begin, const char * in_end, const char *& out_begin, const char *& out_end, PaletteIndex & paletteIndex) -> bool
		{
			paletteIndex = PaletteIndex::Max;

			while (in_begin < in_end && isascii(*in_begin) && isblank(*in_begin))
				in_begin++;

			if (in_begin == in_end)
			{
				out_begin = in_end;
				out_end = in_end;
				paletteIndex = PaletteIndex::Default;
			}
			else if (TokenizeCStyleString(in_begin, in_end, out_begin, out_end))
				paletteIndex = PaletteIndex::String;
			else if (TokenizeCStyleCharacterLiteral(in_begin, in_end, out_begin, out_end))
				paletteIndex = PaletteIndex::CharLiteral;
			else if (TokenizeCStyleIdentifier(in_begin, in_end, out_begin, out_end))
				paletteIndex = PaletteIndex::Identifier;
			else if (TokenizeCStyleNumber(in_begin, in_end, out_begin, out_end))
				paletteIndex = PaletteIndex::Number;
			else if (TokenizeCStylePunctuation(in_begin, in_end, out_begin, out_end))
				paletteIndex = PaletteIndex::Punctuation;

			return paletteIndex != PaletteIndex::Max;
		};

		langDef.mCommentStart = "/*";
		langDef.mCommentEnd = "*/";
		langDef.mSingleLineComment = "//";

		langDef.mCaseSensitive = true;
		langDef.mAutoIndentation = true;

		langDef.mName = "C++";

		inited = true;
	}
	return langDef;
}

const TextEditor::LanguageDefinition& TextEditor::LanguageDefinition::HLSL()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const char* const keywords[] = {
			"AppendStructuredBuffer", "asm", "asm_fragment", "BlendState", "bool", "break", "Buffer", "ByteAddressBuffer", "case", "cbuffer", "centroid", "class", "column_major", "compile", "compile_fragment",
			"CompileShader", "const", "continue", "ComputeShader", "ConsumeStructuredBuffer", "default", "DepthStencilState", "DepthStencilView", "discard", "do", "double", "DomainShader", "dword", "else",
			"export", "extern", "false", "float", "for", "fxgroup", "GeometryShader", "groupshared", "half", "Hullshader", "if", "in", "inline", "inout", "InputPatch", "int", "interface", "line", "lineadj",
			"linear", "LineStream", "matrix", "min16float", "min10float", "min16int", "min12int", "min16uint", "namespace", "nointerpolation", "noperspective", "NULL", "out", "OutputPatch", "packoffset",
			"pass", "pixelfragment", "PixelShader", "point", "PointStream", "precise", "RasterizerState", "RenderTargetView", "return", "register", "row_major", "RWBuffer", "RWByteAddressBuffer", "RWStructuredBuffer",
			"RWTexture1D", "RWTexture1DArray", "RWTexture2D", "RWTexture2DArray", "RWTexture3D", "sample", "sampler", "SamplerState", "SamplerComparisonState", "shared", "snorm", "stateblock", "stateblock_state",
			"static", "string", "struct", "switch", "StructuredBuffer", "tbuffer", "technique", "technique10", "technique11", "texture", "Texture1D", "Texture1DArray", "Texture2D", "Texture2DArray", "Texture2DMS",
			"Texture2DMSArray", "Texture3D", "TextureCube", "TextureCubeArray", "true", "typedef", "triangle", "triangleadj", "TriangleStream", "uint", "uniform", "unorm", "unsigned", "vector", "vertexfragment",
			"VertexShader", "void", "volatile", "while",
			"bool1","bool2","bool3","bool4","double1","double2","double3","double4", "float1", "float2", "float3", "float4", "int1", "int2", "int3", "int4", "in", "out", "inout",
			"uint1", "uint2", "uint3", "uint4", "dword1", "dword2", "dword3", "dword4", "half1", "half2", "half3", "half4",
			"float1x1","float2x1","float3x1","float4x1","float1x2","float2x2","float3x2","float4x2",
			"float1x3","float2x3","float3x3","float4x3","float1x4","float2x4","float3x4","float4x4",
			"half1x1","half2x1","half3x1","half4x1","half1x2","half2x2","half3x2","half4x2",
			"half1x3","half2x3","half3x3","half4x3","half1x4","half2x4","half3x4","half4x4",
		};
		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const char* const identifiers[] = {
			"abort", "abs", "acos", "all", "AllMemoryBarrier", "AllMemoryBarrierWithGroupSync", "any", "asdouble", "asfloat", "asin", "asint", "asint", "asuint",
			"asuint", "atan", "atan2", "ceil", "CheckAccessFullyMapped", "clamp", "clip", "cos", "cosh", "countbits", "cross", "D3DCOLORtoUBYTE4", "ddx",
			"ddx_coarse", "ddx_fine", "ddy", "ddy_coarse", "ddy_fine", "degrees", "determinant", "DeviceMemoryBarrier", "DeviceMemoryBarrierWithGroupSync",
			"distance", "dot", "dst", "errorf", "EvaluateAttributeAtCentroid", "EvaluateAttributeAtSample", "EvaluateAttributeSnapped", "exp", "exp2",
			"f16tof32", "f32tof16", "faceforward", "firstbithigh", "firstbitlow", "floor", "fma", "fmod", "frac", "frexp", "fwidth", "GetRenderTargetSampleCount",
			"GetRenderTargetSamplePosition", "GroupMemoryBarrier", "GroupMemoryBarrierWithGroupSync", "InterlockedAdd", "InterlockedAnd", "InterlockedCompareExchange",
			"InterlockedCompareStore", "InterlockedExchange", "InterlockedMax", "InterlockedMin", "InterlockedOr", "InterlockedXor", "isfinite", "isinf", "isnan",
			"ldexp", "length", "lerp", "lit", "log", "log10", "log2", "mad", "max", "min", "modf", "msad4", "mul", "noise", "normalize", "pow", "printf",
			"Process2DQuadTessFactorsAvg", "Process2DQuadTessFactorsMax", "Process2DQuadTessFactorsMin", "ProcessIsolineTessFactors", "ProcessQuadTessFactorsAvg",
			"ProcessQuadTessFactorsMax", "ProcessQuadTessFactorsMin", "ProcessTriTessFactorsAvg", "ProcessTriTessFactorsMax", "ProcessTriTessFactorsMin",
			"radians", "rcp", "reflect", "refract", "reversebits", "round", "rsqrt", "saturate", "sign", "sin", "sincos", "sinh", "smoothstep", "sqrt", "step",
			"tan", "tanh", "tex1D", "tex1D", "tex1Dbias", "tex1Dgrad", "tex1Dlod", "tex1Dproj", "tex2D", "tex2D", "tex2Dbias", "tex2Dgrad", "tex2Dlod", "tex2Dproj",
			"tex3D", "tex3D", "tex3Dbias", "tex3Dgrad", "tex3Dlod", "tex3Dproj", "texCUBE", "texCUBE", "texCUBEbias", "texCUBEgrad", "texCUBElod", "texCUBEproj", "transpose", "trunc"
		};
		for (auto& k : identifiers)
		{
			Identifier id;
			id.mDeclaration = "Built-in function";
			langDef.mIdentifiers.insert(std::make_pair(std::string(k), id));
		}

		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[ \\t]*#[ \\t]*[a-zA-Z_]+", PaletteIndex::Preprocessor));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("L?\\\"(\\\\.|[^\\\"])*\\\"", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("\\'\\\\?[^\\']\\'", PaletteIndex::CharLiteral));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[+-]?[0-9]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("0[0-7]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[a-zA-Z_][a-zA-Z0-9_]*", PaletteIndex::Identifier));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]", PaletteIndex::Punctuation));

		langDef.mCommentStart = "/*";
		langDef.mCommentEnd = "*/";
		langDef.mSingleLineComment = "//";

		langDef.mCaseSensitive = true;
		langDef.mAutoIndentation = true;

		langDef.mName = "HLSL";

		inited = true;
	}
	return langDef;
}

const TextEditor::LanguageDefinition& TextEditor::LanguageDefinition::GLSL()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const char* const keywords[] = {
			"auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long", "register", "restrict", "return", "short",
			"signed", "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while", "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic", "_Imaginary",
			"_Noreturn", "_Static_assert", "_Thread_local"
		};
		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const char* const identifiers[] = {
			"abort", "abs", "acos", "asin", "atan", "atexit", "atof", "atoi", "atol", "ceil", "clock", "cosh", "ctime", "div", "exit", "fabs", "floor", "fmod", "getchar", "getenv", "isalnum", "isalpha", "isdigit", "isgraph",
			"ispunct", "isspace", "isupper", "kbhit", "log10", "log2", "log", "memcmp", "modf", "pow", "putchar", "putenv", "puts", "rand", "remove", "rename", "sinh", "sqrt", "srand", "strcat", "strcmp", "strerror", "time", "tolower", "toupper"
		};
		for (auto& k : identifiers)
		{
			Identifier id;
			id.mDeclaration = "Built-in function";
			langDef.mIdentifiers.insert(std::make_pair(std::string(k), id));
		}

		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[ \\t]*#[ \\t]*[a-zA-Z_]+", PaletteIndex::Preprocessor));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("L?\\\"(\\\\.|[^\\\"])*\\\"", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("\\'\\\\?[^\\']\\'", PaletteIndex::CharLiteral));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[+-]?[0-9]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("0[0-7]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[a-zA-Z_][a-zA-Z0-9_]*", PaletteIndex::Identifier));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]", PaletteIndex::Punctuation));

		langDef.mCommentStart = "/*";
		langDef.mCommentEnd = "*/";
		langDef.mSingleLineComment = "//";

		langDef.mCaseSensitive = true;
		langDef.mAutoIndentation = true;

		langDef.mName = "GLSL";

		inited = true;
	}
	return langDef;
}

const TextEditor::LanguageDefinition& TextEditor::LanguageDefinition::C()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const char* const keywords[] = {
			"auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long", "register", "restrict", "return", "short",
			"signed", "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while", "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic", "_Imaginary",
			"_Noreturn", "_Static_assert", "_Thread_local"
		};
		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const char* const identifiers[] = {
			"abort", "abs", "acos", "asin", "atan", "atexit", "atof", "atoi", "atol", "ceil", "clock", "cosh", "ctime", "div", "exit", "fabs", "floor", "fmod", "getchar", "getenv", "isalnum", "isalpha", "isdigit", "isgraph",
			"ispunct", "isspace", "isupper", "kbhit", "log10", "log2", "log", "memcmp", "modf", "pow", "putchar", "putenv", "puts", "rand", "remove", "rename", "sinh", "sqrt", "srand", "strcat", "strcmp", "strerror", "time", "tolower", "toupper"
		};
		for (auto& k : identifiers)
		{
			Identifier id;
			id.mDeclaration = "Built-in function";
			langDef.mIdentifiers.insert(std::make_pair(std::string(k), id));
		}

		langDef.mTokenize = [](const char * in_begin, const char * in_end, const char *& out_begin, const char *& out_end, PaletteIndex & paletteIndex) -> bool
		{
			paletteIndex = PaletteIndex::Max;

			while (in_begin < in_end && isascii(*in_begin) && isblank(*in_begin))
				in_begin++;

			if (in_begin == in_end)
			{
				out_begin = in_end;
				out_end = in_end;
				paletteIndex = PaletteIndex::Default;
			}
			else if (TokenizeCStyleString(in_begin, in_end, out_begin, out_end))
				paletteIndex = PaletteIndex::String;
			else if (TokenizeCStyleCharacterLiteral(in_begin, in_end, out_begin, out_end))
				paletteIndex = PaletteIndex::CharLiteral;
			else if (TokenizeCStyleIdentifier(in_begin, in_end, out_begin, out_end))
				paletteIndex = PaletteIndex::Identifier;
			else if (TokenizeCStyleNumber(in_begin, in_end, out_begin, out_end))
				paletteIndex = PaletteIndex::Number;
			else if (TokenizeCStylePunctuation(in_begin, in_end, out_begin, out_end))
				paletteIndex = PaletteIndex::Punctuation;

			return paletteIndex != PaletteIndex::Max;
		};

		langDef.mCommentStart = "/*";
		langDef.mCommentEnd = "*/";
		langDef.mSingleLineComment = "//";

		langDef.mCaseSensitive = true;
		langDef.mAutoIndentation = true;

		langDef.mName = "C";

		inited = true;
	}
	return langDef;
}

const TextEditor::LanguageDefinition& TextEditor::LanguageDefinition::SQL()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const char* const keywords[] = {
			"ADD", "EXCEPT", "PERCENT", "ALL", "EXEC", "PLAN", "ALTER", "EXECUTE", "PRECISION", "AND", "EXISTS", "PRIMARY", "ANY", "EXIT", "PRINT", "AS", "FETCH", "PROC", "ASC", "FILE", "PROCEDURE",
			"AUTHORIZATION", "FILLFACTOR", "PUBLIC", "BACKUP", "FOR", "RAISERROR", "BEGIN", "FOREIGN", "READ", "BETWEEN", "FREETEXT", "READTEXT", "BREAK", "FREETEXTTABLE", "RECONFIGURE",
			"BROWSE", "FROM", "REFERENCES", "BULK", "FULL", "REPLICATION", "BY", "FUNCTION", "RESTORE", "CASCADE", "GOTO", "RESTRICT", "CASE", "GRANT", "RETURN", "CHECK", "GROUP", "REVOKE",
			"CHECKPOINT", "HAVING", "RIGHT", "CLOSE", "HOLDLOCK", "ROLLBACK", "CLUSTERED", "IDENTITY", "ROWCOUNT", "COALESCE", "IDENTITY_INSERT", "ROWGUIDCOL", "COLLATE", "IDENTITYCOL", "RULE",
			"COLUMN", "IF", "SAVE", "COMMIT", "IN", "SCHEMA", "COMPUTE", "INDEX", "SELECT", "CONSTRAINT", "INNER", "SESSION_USER", "CONTAINS", "INSERT", "SET", "CONTAINSTABLE", "INTERSECT", "SETUSER",
			"CONTINUE", "INTO", "SHUTDOWN", "CONVERT", "IS", "SOME", "CREATE", "JOIN", "STATISTICS", "CROSS", "KEY", "SYSTEM_USER", "CURRENT", "KILL", "TABLE", "CURRENT_DATE", "LEFT", "TEXTSIZE",
			"CURRENT_TIME", "LIKE", "THEN", "CURRENT_TIMESTAMP", "LINENO", "TO", "CURRENT_USER", "LOAD", "TOP", "CURSOR", "NATIONAL", "TRAN", "DATABASE", "NOCHECK", "TRANSACTION",
			"DBCC", "NONCLUSTERED", "TRIGGER", "DEALLOCATE", "NOT", "TRUNCATE", "DECLARE", "NULL", "TSEQUAL", "DEFAULT", "NULLIF", "UNION", "DELETE", "OF", "UNIQUE", "DENY", "OFF", "UPDATE",
			"DESC", "OFFSETS", "UPDATETEXT", "DISK", "ON", "USE", "DISTINCT", "OPEN", "USER", "DISTRIBUTED", "OPENDATASOURCE", "VALUES", "DOUBLE", "OPENQUERY", "VARYING","DROP", "OPENROWSET", "VIEW",
			"DUMMY", "OPENXML", "WAITFOR", "DUMP", "OPTION", "WHEN", "ELSE", "OR", "WHERE", "END", "ORDER", "WHILE", "ERRLVL", "OUTER", "WITH", "ESCAPE", "OVER", "WRITETEXT"
		};

		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const char* const identifiers[] = {
			"ABS",  "ACOS",  "ADD_MONTHS",  "ASCII",  "ASCIISTR",  "ASIN",  "ATAN",  "ATAN2",  "AVG",  "BFILENAME",  "BIN_TO_NUM",  "BITAND",  "CARDINALITY",  "CASE",  "CAST",  "CEIL",
			"CHARTOROWID",  "CHR",  "COALESCE",  "COMPOSE",  "CONCAT",  "CONVERT",  "CORR",  "COS",  "COSH",  "COUNT",  "COVAR_POP",  "COVAR_SAMP",  "CUME_DIST",  "CURRENT_DATE",
			"CURRENT_TIMESTAMP",  "DBTIMEZONE",  "DECODE",  "DECOMPOSE",  "DENSE_RANK",  "DUMP",  "EMPTY_BLOB",  "EMPTY_CLOB",  "EXP",  "EXTRACT",  "FIRST_VALUE",  "FLOOR",  "FROM_TZ",  "GREATEST",
			"GROUP_ID",  "HEXTORAW",  "INITCAP",  "INSTR",  "INSTR2",  "INSTR4",  "INSTRB",  "INSTRC",  "LAG",  "LAST_DAY",  "LAST_VALUE",  "LEAD",  "LEAST",  "LENGTH",  "LENGTH2",  "LENGTH4",
			"LENGTHB",  "LENGTHC",  "LISTAGG",  "LN",  "LNNVL",  "LOCALTIMESTAMP",  "LOG",  "LOWER",  "LPAD",  "LTRIM",  "MAX",  "MEDIAN",  "MIN",  "MOD",  "MONTHS_BETWEEN",  "NANVL",  "NCHR",
			"NEW_TIME",  "NEXT_DAY",  "NTH_VALUE",  "NULLIF",  "NUMTODSINTERVAL",  "NUMTOYMINTERVAL",  "NVL",  "NVL2",  "POWER",  "RANK",  "RAWTOHEX",  "REGEXP_COUNT",  "REGEXP_INSTR",
			"REGEXP_REPLACE",  "REGEXP_SUBSTR",  "REMAINDER",  "REPLACE",  "ROUND",  "ROWNUM",  "RPAD",  "RTRIM",  "SESSIONTIMEZONE",  "SIGN",  "SIN",  "SINH",
			"SOUNDEX",  "SQRT",  "STDDEV",  "SUBSTR",  "SUM",  "SYS_CONTEXT",  "SYSDATE",  "SYSTIMESTAMP",  "TAN",  "TANH",  "TO_CHAR",  "TO_CLOB",  "TO_DATE",  "TO_DSINTERVAL",  "TO_LOB",
			"TO_MULTI_BYTE",  "TO_NCLOB",  "TO_NUMBER",  "TO_SINGLE_BYTE",  "TO_TIMESTAMP",  "TO_TIMESTAMP_TZ",  "TO_YMINTERVAL",  "TRANSLATE",  "TRIM",  "TRUNC", "TZ_OFFSET",  "UID",  "UPPER",
			"USER",  "USERENV",  "VAR_POP",  "VAR_SAMP",  "VARIANCE",  "VSIZE "
		};
		for (auto& k : identifiers)
		{
			Identifier id;
			id.mDeclaration = "Built-in function";
			langDef.mIdentifiers.insert(std::make_pair(std::string(k), id));
		}

		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("L?\\\"(\\\\.|[^\\\"])*\\\"", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("\\\'[^\\\']*\\\'", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[+-]?[0-9]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("0[0-7]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[a-zA-Z_][a-zA-Z0-9_]*", PaletteIndex::Identifier));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]", PaletteIndex::Punctuation));

		langDef.mCommentStart = "/*";
		langDef.mCommentEnd = "*/";
		langDef.mSingleLineComment = "//";

		langDef.mCaseSensitive = false;
		langDef.mAutoIndentation = false;

		langDef.mName = "SQL";

		inited = true;
	}
	return langDef;
}

const TextEditor::LanguageDefinition& TextEditor::LanguageDefinition::AngelScript()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const char* const keywords[] = {
			"and", "abstract", "auto", "bool", "break", "case", "cast", "class", "const", "continue", "default", "do", "double", "else", "enum", "false", "final", "float", "for",
			"from", "funcdef", "function", "get", "if", "import", "in", "inout", "int", "interface", "int8", "int16", "int32", "int64", "is", "mixin", "namespace", "not",
			"null", "or", "out", "override", "private", "protected", "return", "set", "shared", "super", "switch", "this ", "true", "typedef", "uint", "uint8", "uint16", "uint32",
			"uint64", "void", "while", "xor"
		};

		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const char* const identifiers[] = {
			"cos", "sin", "tab", "acos", "asin", "atan", "atan2", "cosh", "sinh", "tanh", "log", "log10", "pow", "sqrt", "abs", "ceil", "floor", "fraction", "closeTo", "fpFromIEEE", "fpToIEEE",
			"complex", "opEquals", "opAddAssign", "opSubAssign", "opMulAssign", "opDivAssign", "opAdd", "opSub", "opMul", "opDiv"
		};
		for (auto& k : identifiers)
		{
			Identifier id;
			id.mDeclaration = "Built-in function";
			langDef.mIdentifiers.insert(std::make_pair(std::string(k), id));
		}

		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("L?\\\"(\\\\.|[^\\\"])*\\\"", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("\\'\\\\?[^\\']\\'", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[+-]?[0-9]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("0[0-7]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[a-zA-Z_][a-zA-Z0-9_]*", PaletteIndex::Identifier));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]", PaletteIndex::Punctuation));

		langDef.mCommentStart = "/*";
		langDef.mCommentEnd = "*/";
		langDef.mSingleLineComment = "//";

		langDef.mCaseSensitive = true;
		langDef.mAutoIndentation = true;

		langDef.mName = "AngelScript";

		inited = true;
	}
	return langDef;
}

const TextEditor::LanguageDefinition& TextEditor::LanguageDefinition::Lua()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const char* const keywords[] = {
			"and", "break", "do", "", "else", "elseif", "end", "false", "for", "function", "if", "in", "", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while"
		};

		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const char* const identifiers[] = {
			"assert", "collectgarbage", "dofile", "error", "getmetatable", "ipairs", "loadfile", "load", "loadstring",  "next",  "pairs",  "pcall",  "print",  "rawequal",  "rawlen",  "rawget",  "rawset",
			"select",  "setmetatable",  "tonumber",  "tostring",  "type",  "xpcall",  "_G",  "_VERSION","arshift", "band", "bnot", "bor", "bxor", "btest", "extract", "lrotate", "lshift", "replace",
			"rrotate", "rshift", "create", "resume", "running", "status", "wrap", "yield", "isyieldable", "debug","getuservalue", "gethook", "getinfo", "getlocal", "getregistry", "getmetatable",
			"getupvalue", "upvaluejoin", "upvalueid", "setuservalue", "sethook", "setlocal", "setmetatable", "setupvalue", "traceback", "close", "flush", "input", "lines", "open", "output", "popen",
			"read", "tmpfile", "type", "write", "close", "flush", "lines", "read", "seek", "setvbuf", "write", "__gc", "__tostring", "abs", "acos", "asin", "atan", "ceil", "cos", "deg", "exp", "tointeger",
			"floor", "fmod", "ult", "log", "max", "min", "modf", "rad", "random", "randomseed", "sin", "sqrt", "string", "tan", "type", "atan2", "cosh", "sinh", "tanh",
			"pow", "frexp", "ldexp", "log10", "pi", "huge", "maxinteger", "mininteger", "loadlib", "searchpath", "seeall", "preload", "cpath", "path", "searchers", "loaded", "module", "require", "clock",
			"date", "difftime", "execute", "exit", "getenv", "remove", "rename", "setlocale", "time", "tmpname", "byte", "char", "dump", "find", "format", "gmatch", "gsub", "len", "lower", "match", "rep",
			"reverse", "sub", "upper", "pack", "packsize", "unpack", "concat", "maxn", "insert", "pack", "unpack", "remove", "move", "sort", "offset", "codepoint", "char", "len", "codes", "charpattern",
			"coroutine", "table", "io", "os", "string", "utf8", "bit32", "math", "debug", "package"
		};
		for (auto& k : identifiers)
		{
			Identifier id;
			id.mDeclaration = "Built-in function";
			langDef.mIdentifiers.insert(std::make_pair(std::string(k), id));
		}

		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("L?\\\"(\\\\.|[^\\\"])*\\\"", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("\\\'[^\\\']*\\\'", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[+-]?[0-9]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[a-zA-Z_][a-zA-Z0-9_]*", PaletteIndex::Identifier));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::string, PaletteIndex>("[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]", PaletteIndex::Punctuation));

		langDef.mCommentStart = "--[[";
		langDef.mCommentEnd = "]]";
		langDef.mSingleLineComment = "--";

		langDef.mCaseSensitive = true;
		langDef.mAutoIndentation = false;

		langDef.mName = "Lua";

		inited = true;
	}
	return langDef;
}
