\NeedsTeXFormat{LaTeX2e}
\ProvidesClass{hpman}[2015/08/01 HPCN Report manual]

\def\@titlebreak{\newpage}
\def\@titlefill{\vfill}
\def\@vcsinfo{}
\def\@setupcounter{}
\def\@setupheadings{}
\def\thesectionname{}
\def\thechaptername{}

\newif\if@titlepage

\DeclareOption{notitlepage}{
	\def\@titlebreak{}
	\def\@titlefill{\vspace{30pt}}
	\@titlepagefalse
}

\DeclareOption{titlepage}{
	\@titlepagetrue
}

\DeclareOption{gitinfo}{
	\def\@vcsinfo{Document build from revision \gitAbbrevHash\ \gitReferences\ dated \gitAuthorDate}
}

\DeclareOption{nochapters}{
	\def\@setupcounter{%
		\counterwithout{section}{chapter}
		\counterwithout{figure}{chapter}
		\let\oldappendix\appendix
		\renewcommand{\appendix}{%
			\oldappendix%
			\renewcommand{\thesection}{\Alph{section}}%
		}
	}
	\def\@setupheadings{%
		\makeevenhead{headings}{\thepage}{}{\textit{\thetitle\ \thesubtitle}}
	}
}

\DeclareOption{oneside}{
	\def\@setupheadings{%
		\addtopsmarks{headings}{}{
			\createmark{section}   {right}{shownumber}{}{. \ }
		}
      	\makeoddhead{headings}{\thepage}{}{\rightmark}
	}
	\PassOptionsToClass{oneside}{memoir}
}

\DeclareOption*{\PassOptionsToClass{\CurrentOption}{memoir}}

\ExecuteOptions{titlepage}
\ProcessOptions\relax

\LoadClass[10pt,a4paper]{memoir}

\let\fref\relax

\RequirePackage{graphicx}
\RequirePackage[left=2cm, right=2cm, top=2cm, bottom=1.5cm]{geometry}
\RequirePackage[missing={None}]{gitinfo}
\RequirePackage{imakeidx}
\RequirePackage{hyperref}
\RequirePackage[usenames,dvipsnames]{xcolor}
\RequirePackage[plain]{fancyref}
\RequirePackage{enumitem}
\RequirePackage{ifthen}
\RequirePackage{truncate}
\RequirePackage{environ}

\hypersetup{
	hyperindex,
    colorlinks,
    allcolors=blue!50!black
}

\@setupcounter
\@setupheadings

% Set some nice fonts
\renewcommand{\rmdefault}{ppl}
\renewcommand{\sfdefault}{lmss}
\renewcommand{\ttdefault}{lmtt}

% Improve paragraph spacing
\setlength{\parskip}{3pt}

% List formatting
\setlist{itemsep=2pt, parsep=3pt}

% Prepare the title page
\newcommand{\subtitle}[1]{\renewcommand{\thesubtitle}{#1}}
\newcommand{\thesubtitle}{}
\newcommand{\authormail}[1]{\renewcommand{\theauthormail}{#1}}
\newcommand{\theauthormail}{}

\newif\if@abstractset

\if@titlepage
	% renew the abstract environment to print it in the title page.
	\let\abstract\@undefined
	\let\endabstract\@undefined
	\NewEnviron{abstract}{
		\global\@abstractsettrue
		\global\let\@abstractbody\BODY	% Store the abstract in \@abstractbody
	}
\fi

\renewcommand{\maketitle}{
	\thispagestyle{empty}
	\begin{figure*}[!ht]
	\centering
	\includegraphics[width=50pt]{figs/hpcn_logo.png}
	\end{figure*}
	%
	\begin{center}
	\vspace{-15pt}
	\textsc{\Large High Performance Computing and Networking Research Group}
	\par \vspace{10pt}
	{\large Universidad Aut\'onoma de Madrid} \\
	{Escuela Polit\'ecnica Superior}

	\vspace{30pt}
	\textbf{\LARGE \thetitle}\\ \vspace{3pt}
	{\large \thesubtitle} \\ \vspace{2pt}
	\textit{\scriptsize\@vcsinfo}

	\if@abstractset
		\vspace{5pt}
		\textbf{Abstract}
		\vspace{10pt}

		\parbox{13cm}{\small\@abstractbody}
	\fi

	\@titlefill

	\textbf{\Large \theauthor}\\
	\textit{\large \theauthormail} \par \vspace{4pt}
	{\large \thedate}

	\end{center}
	\@titlebreak
}

% Commands for code objects
\newcommand{\codeobj}[1]{\index{#1}\texttt{#1}}
\newcommand{\branch}[1]{\index{#1}\textit{#1}}
\newcommand{\fileobj}[1]{\index{#1}\textit{#1}}
\newcommand{\sysobj}[1]{\index{#1}\textit{#1}}
\newcommand{\termcommand}[1]{\indent\texttt{> #1}}

% Some command for lists
\newcommand{\itemdesc}[1]{\index{#1}\item \textbf{#1}\,}
\newcommand{\itemdesctt}[1]{\index{#1}\item \texttt{#1}\,}
\newcommand{\itemdescit}[1]{\index{#1}\item \textit{#1}\,}

% Bug information
\newenvironment{bugdata}{\begin{tabular}{ll}}{\end{tabular}}
\newcommand{\gitcommit}[2]{\textbf{Commit:} & #1 - \textit{branch #2} \\}
\newcommand{\bugfixed}[1][]{\textbf{Status:} & \textcolor{OliveGreen}{fixed}\ifthenelse{\equal{#1}{}}{}{ (\textit{commit: \truncate{300pt}{#1}})} \\}
\newcommand{\bugnotfixed}{\textbf{Status:} & \textcolor{BrickRed}{not fixed}\\}
\newcommand{\versions}[1]{\textbf{Affected versions:} & #1\\}
% Section numbering
\settocdepth{subsection}
\setcounter{secnumdepth}{3}

\pagestyle{headings}

% For input of gnuplot files. Automatically resizes the image based on linewidth.
% Note: use latexmk to compile so it detects the dependency with .gp wiles.
\newcommand{\inputgnuplot}[2][1]{
	\vspace{-2ex}
	\resizebox{#1\linewidth}{!}{
		\IfFileExists{#2.tex}{
			\fontsize{7pt}{7pt}\fontfamily{ppl}\selectfont
			\input{#2.tex}
			\vspace{-15pt} % Adjust for font size
		}{
			\PackageWarning{hpman}{File `#2.tex' not found.}
			\fbox{Gnuplot not found.\begin{minipage}{4.7in}\hfill\vspace{3in}\end{minipage}}
		}
	}
	\vspace{-3ex}
}
