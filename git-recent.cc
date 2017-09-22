#include <algorithm>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

#include <getopt.h>
#include <git2.h>

struct branch {
	std::string name;
	bool current;
	time_t last;

	branch(std::string n, bool c, time_t l)
		: name(n), current(c), last(l)
	{
	}

	bool operator<(const struct branch &b) const
	{
		return last > b.last;
	}
};

enum {
	OPTION_HELP,
	OPTION_ALL,
	OPTION_REMOTE,
};

static struct option options[] = {
	{ "help",		no_argument,		0, OPTION_HELP           },
	{ "all",		no_argument,		0, OPTION_ALL            },
	{ "remote",		required_argument,	0, OPTION_REMOTE         },
	{ 0,			0,			0, 0                     }
};

static void usage(const char *cmd)
{
	std::cout << "Usage: " << cmd << " [options]" << std::endl;
	std::cout << "Options:" << std::endl;
	std::cout << "  --help, -h             Print this help message" << std::endl;
	std::cout << "  --all, -a              Also show remote branches" << std::endl;
	std::cout << "  --remote, -r <remote>  Only show branches of a given remote" << std::endl;
}

bool is_prefix(std::string str, std::string prefix)
{
	if (str.size() < prefix.size())
		return false;

	return (str.substr(0, prefix.size()) == prefix);
}

int main(int argc, char **argv)
{
	git_branch_t flags = GIT_BRANCH_LOCAL;
	std::string::size_type max_len = 0;
	std::vector<branch> results;
	git_repository *repo = NULL;
	git_branch_iterator *it;
	git_branch_t ref_type;
	git_reference *ref;
	std::string prefix;
	int error;

	while (true) {
		int c, opt_idx;

		c = getopt_long(argc, argv, "har:", options, &opt_idx);
		if (c == -1)
			break;

		switch (c) {
		case OPTION_HELP:
		case 'h':
			usage(argv[0]);
			return 0;
			break;
		case OPTION_ALL:
		case 'a':
			flags = GIT_BRANCH_ALL;
			break;
		case OPTION_REMOTE:
		case 'r':
			flags = GIT_BRANCH_REMOTE;
			prefix = std::string(optarg) + '/';
			break;
		default:
			usage(argv[0]);
			return 1;
		}
	}
	git_libgit2_init();

	error = git_repository_open(&repo, ".");
	if (error < 0)
		goto err;

	error = git_branch_iterator_new(&it, repo, flags);
	if (error < 0)
		goto err;

	while (git_branch_next(&ref, &ref_type, it) == 0) {
		git_commit *commit;
		const git_oid *oid;
		std::string sname;
		const char *name;

		error = git_branch_name(&name, ref);
		if (error < 0)
			goto err;

		sname = name;
		max_len = std::max(max_len, sname.size());

		if (!is_prefix(sname, prefix))
			continue;

		oid = git_reference_target(ref);
		if (oid == NULL) {
			std::cerr << "Can't get commit for branch " << sname << std::endl;
			continue;
		}

		error = git_commit_lookup(&commit, repo, oid);
		if (error < 0)
			goto err;

		results.emplace_back(branch(name,
					    (git_branch_is_head(ref) == 1),
					    static_cast<time_t>(git_commit_time(commit))));

		git_commit_free(commit);
	}

	git_branch_iterator_free(it);

	std::sort(results.begin(), results.end());

	for (auto &b : results) {
		std::string prefix = b.current ? "* " : "  ";
		struct tm *tm;
		char t[32];

		tm = localtime(&b.last);
		strftime(t, 32, "%Y-%m-%d %H:%M:%S", tm);
		std::cout << prefix << std::left << std::setw(max_len + 2) << b.name << "(" << t << ")" << std::endl;
	}

	git_repository_free(repo);
	git_libgit2_shutdown();

	return 0;

err:
	if (error < 0) {
		const git_error *e = giterr_last();
		std::cerr << "Error: " << e->message << std::endl;
	}

	if (it)
		git_branch_iterator_free(it);

	if (repo)
		git_repository_free(repo);

	git_libgit2_shutdown();

	return 1;
}
