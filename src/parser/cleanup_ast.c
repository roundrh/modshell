#include"cleanup_ast.h"

 /**
  * @param node pointer to ast node.
  *
  * @return always returns 0
  * @brief cleans up all encapsulated data within ast node "node"
  */
int cleanup_ast_node(t_ast_n* node){

    if(!node){
        return -1;
    }

    if(node->sub_ast_root != NULL){
        cleanup_ast(node->sub_ast_root);
        node->sub_ast_root = NULL;
    }

    if(node->io_redir){
        int i = 0;
        while(node->io_redir[i] != NULL){
            if(node->io_redir[i]->filename)
                free(node->io_redir[i]->filename);
            node->io_redir[i]->filename = NULL;

            free(node->io_redir[i]);
            node->io_redir[i] = NULL;
            i++;
        }
        free(node->io_redir);
        node->io_redir = NULL;
    }

    free(node);
    node = NULL;

    return 0;
}

/**
 * @param node pointer to ast node.
 *
 * @return always returns 0
 * @brief recursively cleans up ast by calling cleanup_ast_node on entire tree.
 */
int cleanup_ast(t_ast_n* node){

    if(!node) 
        return -1;

    if(node->left) 
        cleanup_ast(node->left);
    if(node->right) 
        cleanup_ast(node->right);
    
    cleanup_ast_node(node);
    node = NULL;

    return 0;   
}
