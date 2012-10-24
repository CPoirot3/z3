/*++
Copyright (c) 2012 Microsoft Corporation

Module Name:

    recover_01_tactic.cpp

Abstract:

    Recover 01 variables

    Search for clauses of the form
    p  or q or  x = 0
    ~p or q or  x = k1
    p  or ~q or x = k2
    ~p or ~q or x = k1+k2

    Then, replaces 
    x with k1*y1 + k2*y2
    p with y1=1
    q with y2=1
    where y1 and y2 are fresh 01 variables

    The clauses are also removed.

Author:

    Leonardo de Moura (leonardo) 2012-02-17.

Revision History:

--*/
#include"tactical.h"
#include"th_rewriter.h"
#include"extension_model_converter.h"
#include"filter_model_converter.h"
#include"arith_decl_plugin.h"
#include"expr_substitution.h"
#include"dec_ref_util.h"
#include"ast_smt2_pp.h"

class recover_01_tactic : public tactic {
    struct imp {
        typedef obj_map<func_decl, ptr_vector<app> > var2clauses;
        
        ast_manager & m;
        var2clauses   m_var2clauses;
        arith_util    m_util;
        th_rewriter   m_rw;
        bool          m_produce_models;
        unsigned      m_cls_max_size;
        
        imp(ast_manager & _m, params_ref const & p):
            m(_m),
            m_util(m),
            m_rw(m, p) {
            updt_params_core(p);
        }
        
        void updt_params_core(params_ref const & p) {
            m_cls_max_size   = p.get_uint(":recover-01-max-bits", 10);
        }
        
        void updt_params(params_ref const & p) {
            m_rw.updt_params(p);
            updt_params_core(p);
        }
        
        void set_cancel(bool f) {
            m_rw.set_cancel(f);
        }

        bool save_clause(expr * c) {
            if (!m.is_or(c))
                return false;
            func_decl * x = 0;
            app * cls   = to_app(c);
            if (cls->get_num_args() <= 1 || cls->get_num_args() >= m_cls_max_size)
                return false;
            unsigned sz = cls->get_num_args();
            for (unsigned i = 0; i < sz; i++) {
                expr * lit = cls->get_arg(i);
                expr * lhs, * rhs, * arg;
                if (is_uninterp_const(lit)) {
                    // positive literal
                }
                else if (m.is_not(lit, arg) && is_uninterp_const(arg)) {
                    // negative literal
                }
                else if (x == 0 && m.is_eq(lit, lhs, rhs)) {
                    // x = k  literal
                    if (is_uninterp_const(lhs) && m_util.is_numeral(rhs)) {
                        x = to_app(lhs)->get_decl();
                    }
                    else if (is_uninterp_const(rhs) && m_util.is_numeral(lhs)) {
                        x = to_app(rhs)->get_decl();
                    }
                    else {
                        return false;
                    }
                }
                else {
                    return false;
                }
            }
            
            if (x != 0) {
                var2clauses::obj_map_entry * entry = m_var2clauses.insert_if_not_there2(x, ptr_vector<app>());
                if (entry->get_data().m_value.empty() || entry->get_data().m_value.back()->get_num_args() == cls->get_num_args()) {
                    entry->get_data().m_value.push_back(cls);
                    return true;
                }
            }
            return false;
        }
        
        // temporary fields used by operator() and process
        extension_model_converter * mc1;
        filter_model_converter *    mc2;
        expr_substitution *         subst;
        goal_ref                    new_goal;
        obj_map<expr, expr *>       bool2int;
        
        app * find_zero_cls(func_decl * x, ptr_vector<app> & clauses) {
            ptr_vector<app>::iterator it  = clauses.begin();
            ptr_vector<app>::iterator end = clauses.end();
            for (; it != end; ++it) {
                app * cls = *it;
                unsigned num = cls->get_num_args();
                for (unsigned i = 0; i < num; i++) {
                    expr * lhs, * rhs;
                    if (m.is_eq(cls->get_arg(i), lhs, rhs)) {
                        if (is_uninterp_const(lhs) && m_util.is_zero(rhs))
                            return cls;
                        if (is_uninterp_const(rhs) && m_util.is_zero(lhs))
                            return cls;
                    }
                }
            }
            return 0;
        }
        
        // Find coeff (the k of literal (x = k)) of clause cls.
        // Store in idx the bit-vector representing the literals.
        //  Example: idx = 101   if cls has three boolean literals p1, p2, p3
        //                       where p1 = ~q1, p2 = q2, p3 = ~q3 
        //                       and q1 q2 q3 are the corresponding literals in the
        //                       zero clause.
        // Return false, if the boolean literals of cls cannot be matched with the literals
        // of zero_cls
        bool find_coeff(app * cls, app * zero_cls, unsigned & idx, rational & k) {
            unsigned num = zero_cls->get_num_args();
            if (cls->get_num_args() != num)
                return false;
            idx = 0;
            unsigned val = 1;
            for (unsigned i = 0; i < num; i++) {
                expr * lit = zero_cls->get_arg(i);
                if (m.is_eq(lit))
                    continue;
                // search for lit or ~lit in cls
                unsigned j;
                for (j = 0; j < num; j++) {
                    expr * lit2 = cls->get_arg(j);
                    if (m.is_eq(lit2))
                        continue;
                    if (lit2 == lit)
                        break;
                    if (m.is_complement(lit2, lit)) {
                        idx += val;
                        break;
                    }
                }
                if (j == num)
                    return false; // cls does not contain literal lit
                val *= 2;
            }
            
            // find k
            unsigned i;
            for (i = 0; i < num; i++) {
                expr * lhs, * rhs;
                if (m.is_eq(cls->get_arg(i), lhs, rhs) && (m_util.is_numeral(lhs, k) || m_util.is_numeral(rhs, k)))
                    break;
            }
            if (i == num)
                return false;
            
            return true;
        }
        
        void mk_ivar(expr * lit, expr_ref & def, bool real_ctx) {
            expr * atom;
            bool sign;
            if (m.is_not(lit, atom)) {
                sign = true;
            }
            else {
                atom = lit;
                sign = false;
            }
            SASSERT(is_uninterp_const(atom));
            expr * var;
            if (!bool2int.find(atom, var)) {
                var = m.mk_fresh_const(0, m_util.mk_int());
                new_goal->assert_expr(m_util.mk_le(m_util.mk_numeral(rational(0), true), var));
                new_goal->assert_expr(m_util.mk_le(var, m_util.mk_numeral(rational(1), true)));
                expr * bool_def = m.mk_eq(var, m_util.mk_numeral(rational(1), true));
                subst->insert(atom, bool_def);
                if (m_produce_models) {
                    mc2->insert(to_app(var)->get_decl());
                    mc1->insert(to_app(atom)->get_decl(), bool_def);
                }
                m.inc_ref(atom);
                m.inc_ref(var);
                bool2int.insert(atom, var);
            }
            expr * norm_var = real_ctx ? m_util.mk_to_real(var) : var;
            if (sign)
                def = m_util.mk_sub(m_util.mk_numeral(rational(1), !real_ctx), norm_var);
            else
                def = norm_var;
        }
        
        bool process(func_decl * x, ptr_vector<app> & clauses) {
            unsigned cls_size = clauses.back()->get_num_args();
            unsigned expected_num_clauses = 1 << (cls_size - 1);
            if (clauses.size() < expected_num_clauses) // using < instead of != because we tolerate duplicates
                return false;
            app * zero_cls = find_zero_cls(x, clauses);
            if (zero_cls == 0)
                return false;
            
            buffer<bool>     found; // marks which idx were found
            buffer<rational> idx2coeff;
            found.resize(expected_num_clauses, false);
            idx2coeff.resize(expected_num_clauses); 
            
            ptr_vector<app>::iterator it  = clauses.begin();
            ptr_vector<app>::iterator end = clauses.end();
            for (; it != end; ++it) {
                app * cls = *it;
                unsigned idx; rational k;
                if (!find_coeff(cls, zero_cls, idx, k)) 
                    return false;
                SASSERT(idx < expected_num_clauses);
                if (found[idx] && k != idx2coeff[idx])
                    return false; 
                found[idx] = true;
                idx2coeff[idx] = k;
            }
            
            unsigned num_bits = cls_size - 1;
            // check if idxs are consistent
            for (unsigned idx = 0; idx < expected_num_clauses; idx++) {
                if (!found[idx]) 
                    return false; // case is missing
                rational expected_k;
                unsigned idx_aux = idx;
                unsigned idx_bit = 1;
                for (unsigned j = 0; j < num_bits; j++) {
                    if (idx_aux % 2 == 1) {
                        expected_k += idx2coeff[idx_bit];
                    }
                    idx_aux /= 2;
                    idx_bit *= 2;
                }
                if (idx2coeff[idx] != expected_k)
                    return false;
            }
            
            expr_ref_buffer def_args(m);
            expr_ref def(m);
            bool real_ctx = m_util.is_real(x->get_range());
            unsigned idx_bit = 1;
            for (unsigned i = 0; i < cls_size; i++) {
                expr * lit = zero_cls->get_arg(i);
                if (m.is_eq(lit))
                    continue;
                mk_ivar(lit, def, real_ctx);
                def_args.push_back(m_util.mk_mul(m_util.mk_numeral(idx2coeff[idx_bit], !real_ctx), def));
                idx_bit *= 2;
            }
            
            expr * x_def;
            if (def_args.size() == 1)
                x_def = def_args[0];
            else
                x_def = m_util.mk_add(def_args.size(), def_args.c_ptr());
            
            TRACE("recover_01", tout << x->get_name() << " --> " << mk_ismt2_pp(x_def, m) << "\n";);
            subst->insert(m.mk_const(x), x_def);
            if (m_produce_models) {
                mc1->insert(x, x_def);
            }
            return true;
        }
    
        void operator()(goal_ref const & g, 
                        goal_ref_buffer & result, 
                        model_converter_ref & mc, 
                        proof_converter_ref & pc,
                        expr_dependency_ref & core) {
            SASSERT(g->is_well_sorted());
            fail_if_proof_generation("recover-01", g);
            fail_if_unsat_core_generation("recover-01", g);
            m_produce_models      = g->models_enabled();
            mc = 0; pc = 0; core = 0; result.reset();
            tactic_report report("recover-01", *g);
            
            bool saved = false;
            new_goal = alloc(goal, *g, true);
            SASSERT(new_goal->depth() == g->depth());
            SASSERT(new_goal->prec() == g->prec());
            new_goal->inc_depth();
            
            unsigned sz = g->size();
            for (unsigned i = 0; i < sz; i++) {
                expr * f = g->form(i);
                if (save_clause(f)) {
                    saved = true;
                }
                else {
                    new_goal->assert_expr(f);
                }
            }
            
            if (!saved) {
                result.push_back(g.get());
                return;
            }
            
            if (m_produce_models) {
                mc1 = alloc(extension_model_converter, m);
                mc2 = alloc(filter_model_converter, m);
                mc  = concat(mc2, mc1);
            }
            
            dec_ref_key_values(m, bool2int);
            
            unsigned counter = 0;
            bool recovered = false;
            expr_substitution _subst(m);
            subst = &_subst;
            var2clauses::iterator it  = m_var2clauses.begin();
            var2clauses::iterator end = m_var2clauses.end();
            for (; it != end; ++it) {
                if (process(it->m_key, it->m_value)) {
                    recovered = true;
                    counter++;
                }
                else {
                    ptr_vector<app>::iterator it2   = it->m_value.begin();
                    ptr_vector<app>::iterator end2  = it->m_value.end();
                    for (; it2 != end2; ++it2) {
                        new_goal->assert_expr(*it2);
                    }
                }
            }
            
            if (!recovered) {
                result.push_back(g.get());
                mc = 0;
                return;
            }
            
            report_tactic_progress(":recovered-01-vars", counter);
            
            m_rw.set_substitution(subst);
            expr_ref   new_curr(m);
            proof_ref  new_pr(m);
            unsigned size = new_goal->size();
            for (unsigned idx = 0; idx < size; idx++) {
                expr * curr = new_goal->form(idx);
                m_rw(curr, new_curr);
                new_goal->update(idx, new_curr);
            }
            result.push_back(new_goal.get());
            TRACE("recover_01", new_goal->display(tout););
            SASSERT(new_goal->is_well_sorted());
        }
        
        ~imp() {
            dec_ref_key_values(m, bool2int);
        }
    };
    
    imp *      m_imp;
    params_ref m_params;
public:
    recover_01_tactic(ast_manager & m, params_ref const & p):
        m_params(p) {
        m_imp = alloc(imp, m, p);
    }

    virtual tactic * translate(ast_manager & m) {
        return alloc(recover_01_tactic, m, m_params);
    }
    
    virtual ~recover_01_tactic() {
        dealloc(m_imp);
    }

    virtual void updt_params(params_ref const & p) {
        m_params = p;
        m_imp->updt_params(p);
    }

    virtual void collect_param_descrs(param_descrs & r) { 
        th_rewriter::get_param_descrs(r);
        r.insert(":recover-01-max-bits", CPK_UINT, "(default: 10) maximum number of bits to consider in a clause.");
    }

    void operator()(goal_ref const & g, 
                    goal_ref_buffer & result, 
                    model_converter_ref & mc, 
                    proof_converter_ref & pc,
                    expr_dependency_ref & core) {
        (*m_imp)(g, result, mc, pc, core);
    }
    
    virtual void cleanup() {
        ast_manager & m = m_imp->m;
        imp * d = m_imp;
        #pragma omp critical (tactic_cancel)
        {
            d = m_imp;
        }
        dealloc(d);
        d = alloc(imp, m, m_params);
        #pragma omp critical (tactic_cancel)
        {
            m_imp = d;
        }
    }

protected:
    virtual void set_cancel(bool f) {
        if (m_imp)
            m_imp->set_cancel(f);
    }
};

tactic * mk_recover_01_tactic(ast_manager & m, params_ref const & p) {
    return clean(alloc(recover_01_tactic, m, p));
}