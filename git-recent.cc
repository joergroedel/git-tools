#include <algorithm>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

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

int main(int argc, char **argv)
{
	std::string::size_type max_len = 0;
	std::vector<branch> results;
	git_repository *repo = NULL;
	git_branch_iterator *it;
	git_branch_t ref_type;
	git_reference *ref;
	int error;

	git_libgit2_init();

	error = git_repository_open(&repo, ".");
	if (error < 0)
		goto err;

	error = git_branch_iterator_new(&it, repo, GIT_BRANCH_LOCAL);
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
