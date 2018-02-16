/*
 * TODO:
 *		- Implement --all option
 *		- Implement progress indicator for checkout
 *		- More testing
 *		- Man page
 		- Documentation
 */
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <string>
#include <map>
#include <set>

#include <getopt.h>
#include <git2.h>

struct cb_payload {
	const char *name;
	git_repository *repo;
	git_oid oid;
	bool success;

	cb_payload()
		: name(0), repo(0), success(false)
	{}
};

static int tag_foreach_cb(const char *name, git_oid *oid, void *payload)
{
	struct cb_payload *p = (struct cb_payload *)payload;
	git_tag *tag;
	int error;
	std::string n1("refs/tags/"), n2(name);

	n1 += p->name;

	if (n1 != n2)
		return 0;

	error = git_tag_lookup(&tag, p->repo, oid);
	if (error) {
		std::cerr << "Can't lookup tag " << name << std::endl;
		return 0;
	}

	if (git_tag_target_type(tag) != GIT_OBJ_COMMIT) {
		std::cerr << "Tag " << name << " doesn't point to a commit" << std::endl;
		return 0;
	}

	git_oid_cpy(&p->oid, git_tag_target_id(tag));
	p->success = true;

	git_tag_free(tag);

out:
	return 0;
}

static bool lookup_target(const char *name, git_repository *repo, git_oid *out_oid)
{
	git_reference *target_ref = NULL;
	struct cb_payload p;
	const git_oid *ref_oid;
	bool ret = true;
	std::string ref;
	git_oid oid;
	int error;

	/* First check if it is a commit-id */
	error = git_oid_fromstr(out_oid, name);
	if (error == 0)
		goto out;

	/* Check local and remote branches */
	error = git_branch_lookup(&target_ref, repo, name, GIT_BRANCH_LOCAL);
	if (error == 0)
		goto have_ref;

	error = git_branch_lookup(&target_ref, repo, name, GIT_BRANCH_REMOTE);
	if (error == 0)
		goto have_ref;

	/* Check tags */
	p.name = name;
	p.repo = repo;

	git_tag_foreach(repo, tag_foreach_cb, &p);

	if (!p.success) {
		ret = false;
		goto out;
	}

	git_oid_cpy(out_oid, &p.oid);

out:
	if (target_ref)
		git_reference_free(target_ref);

	return ret;

have_ref:
	ref_oid = git_reference_target(target_ref);
	if (ref_oid == NULL) {
		ret = false;
		goto out;
	}

	git_oid_cpy(out_oid, ref_oid);

	goto out;
}

enum {
	OPTION_HELP,
	OPTION_LIST,
	OPTION_NOT,
	OPTION_ONLY,
};

static struct option options[] = {
	{ "help",		no_argument,		0, OPTION_HELP           },
	{ "list",		no_argument,		0, OPTION_LIST           },
	{ "not",		no_argument,		0, OPTION_NOT            },
	{ "only",		no_argument,		0, OPTION_ONLY           },
	{ 0,			0,			0, 0                     }
};

void usage(const char *cmd)
{
	std::cout << "Usage: " << cmd << " [options] <branches...> <target>" << std::endl;
	std::cout << "Options:" << std::endl;
	std::cout << "  --help, -h             Print this help message" << std::endl;
}

struct result {
	bool ff;
	bool current;
	bool up2date;

	result()
		: ff(false), current(false), up2date(false)
	{}
};

struct parameters {
	bool not_ff;
	bool only_ff;
	bool list;
	bool verbose;

	std::set<std::string> branches;
	const char *target;

	parameters()
		: not_ff(false), only_ff(false), list(false),
		  verbose(true)
	{}
};

static int do_list(git_repository *repo, parameters &params)
{
	git_branch_t flags = GIT_BRANCH_LOCAL;
	std::map<std::string, result> results;
	std::string::size_type max_len = 0;
	git_branch_iterator *it = NULL;
	git_branch_t ref_type;
	git_oid target_oid;
	git_reference *ref;
	int error = 0;

	if (!lookup_target(params.target, repo, &target_oid)) {
		std::cerr << "Can't resolve " << params.target << std::endl;
		goto out;
	}

	error = git_branch_iterator_new(&it, repo, flags);
	if (error < 0)
		goto out;

	while (git_branch_next(&ref, &ref_type, it) == 0) {
		const git_oid *branch_oid;
		const char *name;
		git_oid mb_oid;

		error = git_branch_name(&name, ref);
		if (error < 0)
			goto out;

		if (!params.branches.empty() &&
		     params.branches.find(name) == params.branches.end())
			continue;

		branch_oid = git_reference_target(ref);

		error = git_merge_base(&mb_oid, repo, branch_oid, &target_oid);
		if (error < 0)
			goto out;

		if (git_oid_cmp(branch_oid, &mb_oid) == 0) {
			results[name].ff = true;
		}

		if (git_oid_cmp(branch_oid, &target_oid) == 0) {
			results[name].up2date = true;
		}

		results[name].current = (git_branch_is_head(ref) == 1);
	}

	for (auto &s : results)
		max_len = std::max(s.first.size(), max_len);

	for (auto &s : results) {
		if ((s.second.ff && params.not_ff) ||
		    (!s.second.ff && params.only_ff))
			continue;

		if (params.verbose)
			if (s.second.current)
				std::cout << "* ";
			else
				std::cout << "  ";

		if (!params.verbose) {
			std::cout << s.first << std::endl;
			continue;
		}

		std::cout << std::left << std::setw(max_len + 2) << s.first;

		if (s.second.up2date)
			std::cout << "already on " << params.target;
		else if (s.second.ff)
			std::cout << "fast-forward to " << params.target;
		else
			std::cout << "non-fast-forward to " << params.target;

		std::cout << std::endl;
	}

out:
	if (it)
		git_branch_iterator_free(it);

	return error;
}

static int notify_cb(git_checkout_notify_t why,
		     const char *path,
		     const git_diff_file *baseline,
		     const git_diff_file *target,
		     const git_diff_file *workdir,
		     void *payload)
{
	const char *name = (const char *)payload;

	std::cerr << "Can't fast-forward " << name << ", checkout conflict" << std::endl;

	return 1;
}

static int do_ff(git_repository *repo, parameters &params)
{
	git_branch_t flags = GIT_BRANCH_LOCAL;
	git_branch_iterator *it = NULL;
	git_reference *new_ref;
	git_branch_t ref_type;
	git_oid target_oid;
	git_reference *ref;
	bool head_only;
	int error = 0;

	if (!lookup_target(params.target, repo, &target_oid)) {
		std::cerr << "Can't resolve " << params.target << std::endl;
		goto out;
	}

	error = git_branch_iterator_new(&it, repo, flags);
	if (error < 0)
		goto out;

	head_only = params.branches.empty();

	while (git_branch_next(&ref, &ref_type, it) == 0) {
		const git_oid *branch_oid;
		const char *name;
		git_oid mb_oid;

		if (head_only && (git_branch_is_head(ref) != 1))
			continue;

		error = git_branch_name(&name, ref);
		if (error < 0)
			goto out;

		if (!params.branches.empty() &&
		     params.branches.find(name) == params.branches.end())
			continue;

		branch_oid = git_reference_target(ref);

		error = git_merge_base(&mb_oid, repo, branch_oid, &target_oid);
		if (error < 0)
			goto out;

		if (git_oid_cmp(branch_oid, &mb_oid) != 0) {
			std::cerr << "Not possible to fast-forward " << name << std::endl;
			continue;
		}

		if (git_oid_cmp(branch_oid, &target_oid) == 0) {
			std::cout << "Branch " << name << " already on " << params.target << std::endl;
			continue;
		}

		if (head_only || (git_branch_is_head(ref) == 1)) {
			git_checkout_options opts;
			git_object *obj;

			// Updating HEAD, checkout new work-tree
			error = git_object_lookup(&obj, repo, &target_oid, GIT_OBJ_COMMIT);
			if (error < 0)
				goto out;

			error = git_checkout_init_options(&opts, GIT_CHECKOUT_OPTIONS_VERSION);
			if (error < 0) {
				git_object_free(obj);
				goto out;
			}

			opts.checkout_strategy	= GIT_CHECKOUT_SAFE;
			opts.notify_flags	= GIT_CHECKOUT_NOTIFY_CONFLICT;
			opts.notify_cb		= notify_cb;
			opts.notify_payload	= (void *)name;

			error = git_checkout_tree(repo, obj, &opts);
			if (error < 0) {
				git_object_free(obj);
				goto out;
			}

			git_object_free(obj);

			if (error > 0)
				// Checkout conflict
				continue;
		}

		error = git_reference_set_target(&new_ref, ref, &target_oid, NULL);
		if (error < 0)
			goto out;

		std::cout << "fast-forwared " << name << " to " << params.target << std::endl;

		git_reference_free(new_ref);
	}
out:
	if (it)
		git_branch_iterator_free(it);

	return error;
}

int main(int argc, char **argv)
{
	git_repository *repo = NULL;
	const char *target = NULL;
	int error;

	struct parameters params;

	while (true) {
		int c, opt_idx;

		c = getopt_long(argc, argv, "hlonu", options, &opt_idx);
		if (c == -1)
			break;

		switch (c) {
		case OPTION_HELP:
		case 'h':
			usage(argv[0]);
			return 0;
			break;
		case OPTION_LIST:
		case 'l':
			params.list = true;
			break;
		case OPTION_ONLY:
		case 'o':
			params.only_ff = true;
			params.not_ff  = false;
			params.verbose = false;
			break;
		case OPTION_NOT:
		case 'n':
			params.only_ff = false;
			params.not_ff  = true;
			params.verbose = false;
			break;
		default:
			usage(argv[0]);
			return 1;
		}
	}

	while (optind < argc) {
		if (target)
			params.branches.emplace(std::move(std::string(target)));

		target = argv[optind++];
	}

	if (target == NULL) {
		std::cerr << "Need a fast-forward target" << std::endl;
		usage(argv[0]);
		return 1;
	}

	params.target = target;

	if (!params.list && (params.not_ff || params.only_ff)) {
		std::cerr << "Error: --only and --not require --list" << std::endl;
		usage(argv[0]);
		return 1;
	}

	git_libgit2_init();

	error = git_repository_open(&repo, ".");
	if (error < 0)
		goto err;

	if (params.list)
		error = do_list(repo, params);
	else
		error = do_ff(repo, params);

	if (error)
		goto out_err;

	git_repository_free(repo);

	git_libgit2_shutdown();

	return 0;

err:

	if (error < 0) {
		const git_error *e = giterr_last();
		std::cerr << "Error: " << e->message << std::endl;
	}

out_err:
	if (repo)
		git_repository_free(repo);

	git_libgit2_shutdown();

	return 1;
}
